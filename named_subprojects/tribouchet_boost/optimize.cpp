// optimize.cpp - optimises the move list of a sliding-tile puzzle ("15 puzzle", any R x C, up to 100 x 100).
//
//   ./optimize in.txt out.txt [seconds=120] [threads=auto] [--full]
//       seconds=0: only check ORDER and print the objective      --full: never drop the tail of the order
//
// Input/output follow the formal task "PYATNASHKI 2026":
//   DATA   / SIZE N M / N rows, '=' is the free cell / "/"        starting position
//   COST   / four costs of moving a tile U D L R / "/"            optional, default 1 1 1 1
//   ORDER  / "tile DIR" per line (DIR = direction the TILE moves) / "/"
//   "--" starts a comment (own line or end of a line). The final position is fixed (1..N*M-1, free cell last).
// Objective (maximised):  F = 100 * P - sum(Cd * Nd),  P = tiles on their places after the last move.
//
// Modes:   DATA + ORDER   ORDER is validated (error with the number of the bad move), F is reported and the
//                         solution is optimised.
//          DATA only      no solution to optimise: the tool only says whether the position is solvable.
//
// How the optimisation works. A solution is a walk of the blank. A slice of that walk leading from state A to
// state B can be replaced by any CHEAPER walk from A to B (the final position, hence P, never changes).
// Phase 1 (descent)  for every start i and window length L:
//                      - the cost-weighted Manhattan distance of the tiles that differ between state i and
//                        state i+L is a lower bound. If it is >= the window cost the window is provably optimal
//                        and skipped (O(1)).
//                      - otherwise an exact IDA* search looks for a replacement that costs less.
//                    Passes run with growing window size / node budget (search cost grows ~2x per 2 moves of slack).
// Phase 2 (plateau)  when descent is stuck (local minimum) random windows are rewritten by a DIFFERENT path of the
//                    SAME cost, then the neighbourhood is re-examined by descent. This random walk over equal-cost
//                    solutions moves the solution to where new shortcuts exist.
// Finally the best PREFIX of the order is kept (F is measured after the last move, so when the last tiles cost more
// than 100 points each to place, stopping early scores higher). --full switches this off.
//
// The move list is cut into chunks that are optimised independently (one thread each); chunk borders move
// between passes. Every pass ends with a full replay that checks the final board is unchanged, and the best
// result is written after every pass, so you can stop the program at any time.

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
using namespace std;
typedef unsigned long long u64;
typedef long long i64;

// ---------------------------------------------------------------------------------------------
// Basics
// ---------------------------------------------------------------------------------------------
static const int DR[4] = {-1, 1, 0, 0};  // direction of the BLANK: 0=U 1=D 2=L 3=R
static const int DC[4] = {0, 0, -1, 1};
static const char TILE_CH[4] = {'D', 'U', 'R', 'L'};  // the tile moves opposite to the blank

typedef chrono::steady_clock Clock;
static Clock::time_point T0 = Clock::now();
static double now_s() { return chrono::duration<double>(Clock::now() - T0).count(); }

struct Puzzle {
    int R = 0, C = 0, N = 0;
    vector<uint16_t> start;     // cell -> tile, 0 = free cell
    i64 cost[4] = {1, 1, 1, 1}; // cost of moving a TILE up / down / left / right (COST block, default 1)
    vector<int> ordTile;        // ORDER block as written: tile number ...
    vector<char> ordDir;        // ... and the direction that tile moves (U D L R)
    bool hasOrder = false;
    vector<uint8_t> mv;         // the same moves as blank directions (filled by buildMoves)
};

struct Geometry {  // read-only, shared by all threads
    int R, C, N;
    vector<int> nbr;  // N*4, -1 when off board
    vector<int16_t> rr, cc;
    i64 tc[4];        // cost of a tile moving U D L R
    i64 cb[4];        // cost of a move in which the BLANK moves in direction d (the tile moves the other way)
    i64 unit = 1;     // typical cost of one move; scales the slack limits
    void init(int R_, int C_, const i64* tileCost = nullptr) {
        R = R_; C = C_; N = R * C;
        nbr.assign(N * 4, -1); rr.resize(N); cc.resize(N);
        for (int r = 0; r < R; r++)
            for (int c = 0; c < C; c++) {
                int u = r * C + c;
                rr[u] = (int16_t)r; cc[u] = (int16_t)c;
                for (int d = 0; d < 4; d++) {
                    int r2 = r + DR[d], c2 = c + DC[d];
                    if (r2 >= 0 && r2 < R && c2 >= 0 && c2 < C) nbr[u * 4 + d] = r2 * C + c2;
                }
            }
        for (int d = 0; d < 4; d++) tc[d] = tileCost ? tileCost[d] : 1;
        for (int d = 0; d < 4; d++) cb[d] = tc[d ^ 1];
        unit = max<i64>(1, (tc[0] + tc[1] + tc[2] + tc[3]) / 4);
    }
    // cheapest conceivable cost for a tile to travel from cell a to cell T (each step in a needed direction
    // must be paid at least once; extra steps cost >= 0): admissible lower bound
    // change of wd() when a tile standing on cell nb takes one step in direction td (0=U 1=D 2=L 3=R):
    // a step towards its target saves what that step costs, a step away adds the cost of the way back
    inline i64 dhMove(int td, int nb, int T) const {
        int diff = td < 2 ? (int)rr[T] - rr[nb] : (int)cc[T] - cc[nb];
        bool toward = (td & 1) ? diff > 0 : diff < 0;
        return toward ? -tc[td] : tc[td ^ 1];
    }
    inline i64 wd(int a, int T) const {
        int dr = rr[T] - rr[a], dc = cc[T] - cc[a];
        return (dr > 0 ? dr * tc[1] : -dr * tc[0]) + (dc > 0 ? dc * tc[3] : -dc * tc[2]);
    }
};

static void die(const string& m) { fprintf(stderr, "error: %s\n", m.c_str()); exit(1); }
static void fail(int code, const string& m) { fprintf(stderr, "ERROR: %s\n", m.c_str()); exit(code); }

// ---------------------------------------------------------------------------------------------
// I/O, formal task format (see header). Exit codes: 2 = bad input data, 3 = bad ORDER, 4 = unsolvable.
// ---------------------------------------------------------------------------------------------
struct RawLine { vector<string> tok; bool slash = false; int no = 0; };

static string upper(string s) { for (auto& ch : s) ch = (char)toupper((unsigned char)ch); return s; }
static bool parseInt(const string& s, i64& v) {
    char* e = nullptr;
    v = strtoll(s.c_str(), &e, 10);
    return !s.empty() && *e == 0;
}
static bool isKeyword(const string& t) { string u = upper(t); return u == "DATA" || u == "COST" || u == "ORDER"; }

static vector<RawLine> readLines(const string& path) {
    ifstream in(path);
    if (!in) fail(2, "cannot open " + path);
    vector<RawLine> out;
    string line;
    int no = 0;
    while (getline(in, line)) {
        no++;
        size_t c = line.find("--");
        if (c != string::npos) line.erase(c);          // comment
        RawLine rl; rl.no = no;
        for (auto& ch : line) if (ch == '/') { rl.slash = true; ch = ' '; }   // "/" ends a block
        istringstream ss(line);
        string t;
        while (ss >> t) rl.tok.push_back(t);
        if (!rl.tok.empty() || rl.slash) out.push_back(rl);
    }
    return out;
}

static void loadPuzzle(const string& path, Puzzle& p) {
    vector<RawLine> L = readLines(path);
    bool haveData = false, haveCost = false;
    size_t i = 0;
    while (i < L.size()) {
        if (L[i].tok.empty()) { i++; continue; }       // stray "/"
        const string kw = upper(L[i].tok[0]);
        if (!isKeyword(kw)) fail(2, "line " + to_string(L[i].no) + ": unexpected '" + L[i].tok[0] + "' (expected DATA, COST or ORDER)");
        // collect the block: rest of the keyword line, then lines up to the one holding "/"
        vector<RawLine> blk;
        int kwLine = L[i].no;
        RawLine first = L[i];
        first.tok.erase(first.tok.begin());
        bool closed = L[i].slash;
        if (!first.tok.empty()) blk.push_back(first);
        i++;
        while (!closed && i < L.size()) {
            if (!L[i].tok.empty() && isKeyword(L[i].tok[0])) break;   // block was not closed with "/"
            if (!L[i].tok.empty()) blk.push_back(L[i]);
            closed = L[i].slash;
            i++;
        }
        if (kw == "DATA") {
            if (haveData) fail(2, "line " + to_string(kwLine) + ": second DATA block");
            haveData = true;
            if (blk.empty() || upper(blk[0].tok[0]) != "SIZE" || blk[0].tok.size() != 3)
                fail(2, "line " + to_string(kwLine) + ": DATA must be followed by 'SIZE N M'");
            i64 n, m;
            if (!parseInt(blk[0].tok[1], n) || !parseInt(blk[0].tok[2], m)) fail(2, "line " + to_string(blk[0].no) + ": SIZE needs two integers");
            if (n < 1 || m < 1 || n > 100 || m > 100) fail(2, "SIZE: rows and columns must be in 1..100");
            if (n * m < 2 || n * m - 1 > 10000) fail(2, "SIZE: the board must have 1..10000 tiles");
            p.R = (int)n; p.C = (int)m; p.N = p.R * p.C;
            if ((i64)blk.size() != n + 1) fail(2, "DATA: expected " + to_string(n) + " board rows, found " + to_string((int)blk.size() - 1));
            vector<int> seenTile(p.N, 0);
            int blanks = 0;
            for (int r = 0; r < p.R; r++) {
                const RawLine& rl = blk[r + 1];
                if ((int)rl.tok.size() != p.C)
                    fail(2, "line " + to_string(rl.no) + ": row " + to_string(r + 1) + " has " + to_string(rl.tok.size()) + " entries, expected " + to_string(p.C));
                for (const string& t : rl.tok) {
                    if (t == "=") { p.start.push_back(0); blanks++; continue; }
                    i64 v;
                    if (!parseInt(t, v) || v < 1 || v >= p.N) fail(2, "line " + to_string(rl.no) + ": bad tile '" + t + "' (tiles are 1.." + to_string(p.N - 1) + ")");
                    if (seenTile[v]++) fail(2, "line " + to_string(rl.no) + ": tile " + t + " appears twice");
                    p.start.push_back((uint16_t)v);
                }
            }
            if (blanks != 1) fail(2, "DATA: exactly one free cell '=' required, found " + to_string(blanks));
        } else if (kw == "COST") {
            if (haveCost) fail(2, "line " + to_string(kwLine) + ": second COST block");
            haveCost = true;
            vector<i64> v;
            for (auto& rl : blk) for (auto& t : rl.tok) {
                i64 x;
                if (!parseInt(t, x) || x < 0) fail(2, "line " + to_string(rl.no) + ": COST entries must be non-negative integers");
                v.push_back(x);
            }
            if (v.size() != 4) fail(2, "line " + to_string(kwLine) + ": COST needs four numbers (U D L R)");
            for (int d = 0; d < 4; d++) p.cost[d] = v[d];
        } else {  // ORDER
            if (p.hasOrder) fail(2, "line " + to_string(kwLine) + ": second ORDER block");
            p.hasOrder = true;
            vector<string> t;
            for (auto& rl : blk) for (auto& x : rl.tok) t.push_back(x);
            if (t.size() % 2) fail(3, "ORDER: odd number of entries, every move is 'tile DIR'");
            for (size_t k = 0; k < t.size(); k += 2) {
                i64 tile;
                if (!parseInt(t[k], tile)) fail(3, "ORDER move #" + to_string(k / 2 + 1) + ": '" + t[k] + "' is not a tile number");
                string d = upper(t[k + 1]);
                if (d != "U" && d != "D" && d != "L" && d != "R") fail(3, "ORDER move #" + to_string(k / 2 + 1) + ": bad direction '" + t[k + 1] + "'");
                p.ordTile.push_back((int)min<i64>(tile, INT_MAX));
                p.ordDir.push_back(d[0]);
            }
        }
    }
    if (!haveData) fail(2, "no DATA block");
}

// validate ORDER against DATA (moves are numbered from 1) and convert it to blank directions
static void buildMoves(const Geometry& g, Puzzle& p) {
    vector<int> pos(g.N);
    for (int c = 0; c < g.N; c++) pos[p.start[c]] = c;
    int blank = pos[0];
    p.mv.clear();
    auto rc = [&](int cell) { return "(row " + to_string(cell / g.C + 1) + ", column " + to_string(cell % g.C + 1) + ")"; };
    for (size_t k = 0; k < p.ordTile.size(); k++) {
        int tile = p.ordTile[k];
        char dc = p.ordDir[k];
        string tag = "ORDER move #" + to_string(k + 1) + " (" + to_string(tile) + " " + dc + "): ";
        if (tile < 1 || tile >= g.N) fail(3, tag + "there is no such tile");
        int td = dc == 'U' ? 0 : dc == 'D' ? 1 : dc == 'L' ? 2 : 3;   // direction of the tile
        int bd = td ^ 1;                                              // the blank moves the other way
        int cell = pos[tile];
        if (g.nbr[blank * 4 + bd] != cell) {
            int dist = abs(g.rr[cell] - g.rr[blank]) + abs(g.cc[cell] - g.cc[blank]);
            if (dist != 1) fail(3, tag + "tile is at " + rc(cell) + ", not next to the free cell " + rc(blank));
            fail(3, tag + "tile at " + rc(cell) + " cannot move that way, the free cell is at " + rc(blank));
        }
        pos[tile] = blank;
        blank = cell;
        p.mv.push_back((uint8_t)bd);
    }
}

static i64 pathCost(const Geometry& g, const vector<uint8_t>& mv) {
    i64 c = 0;
    for (uint8_t d : mv) c += g.cb[d];
    return c;
}
static int countPlaced(const Geometry& g, const vector<uint16_t>& B) {   // tiles on their final cell
    int P = 0;
    for (int c = 0; c < g.N - 1; c++) P += (B[c] == c + 1);
    return P;
}
// F after the first k moves
static i64 prefixScore(const Geometry& g, const vector<uint16_t>& start, const vector<uint8_t>& mv, size_t k) {
    vector<uint16_t> B = start;
    int blank = (int)(find(B.begin(), B.end(), 0) - B.begin());
    i64 cost = 0, placed = countPlaced(g, B);
    for (size_t j = 0; j < k; j++) {
        int nb = g.nbr[blank * 4 + mv[j]], t = B[nb];
        placed += (blank == t - 1) - (nb == t - 1);
        cost += g.cb[mv[j]];
        B[blank] = t; B[nb] = 0; blank = nb;
    }
    return 100 * placed - cost;
}
static size_t bestPrefix(const Geometry& g, const vector<uint16_t>& start, const vector<uint8_t>& mv) {
    vector<uint16_t> B = start;
    int blank = (int)(find(B.begin(), B.end(), 0) - B.begin());
    i64 cost = 0, placed = countPlaced(g, B), bestF = 100 * placed;
    size_t bestK = 0;
    for (size_t j = 0; j < mv.size(); j++) {
        int nb = g.nbr[blank * 4 + mv[j]], t = B[nb];
        placed += (blank == t - 1) - (nb == t - 1);
        cost += g.cb[mv[j]];
        B[blank] = t; B[nb] = 0; blank = nb;
        if (100 * placed - cost > bestF) { bestF = 100 * placed - cost; bestK = j + 1; }
    }
    return bestK;
}

// Can the position reach the final one? (inversion parity; the free cell must end bottom-right)
static bool solvable(const Puzzle& p) {
    vector<int> seq;
    int blankRow = 0;
    for (int c = 0; c < p.N; c++) { if (p.start[c] == 0) blankRow = c / p.C; else seq.push_back(p.start[c]); }
    i64 inv = 0;
    vector<int> bit(p.N + 2, 0);
    for (int k = (int)seq.size() - 1; k >= 0; k--) {          // count smaller tiles to the right
        for (int x = seq[k] - 1; x > 0; x -= x & -x) inv += bit[x];
        for (int x = seq[k]; x <= p.N; x += x & -x) bit[x]++;
    }
    if (p.R == 1 || p.C == 1) return inv == 0;
    if (p.C % 2 == 1) return inv % 2 == 0;
    return ((inv + blankRow) % 2) == ((p.R - 1) % 2);
}

static void savePuzzle(const string& path, const Puzzle& p) {
    Geometry g; g.init(p.R, p.C);
    string out = "DATA\nSIZE " + to_string(p.R) + " " + to_string(p.C) + "\n";
    for (int r = 0; r < p.R; r++) {
        for (int c = 0; c < p.C; c++) {
            uint16_t t = p.start[r * p.C + c];
            out += (t ? to_string(t) : string("="));
            out += (c + 1 < p.C ? ' ' : '\n');
        }
    }
    out += "/\nCOST\n" + to_string(p.cost[0]) + " " + to_string(p.cost[1]) + " " + to_string(p.cost[2]) + " " + to_string(p.cost[3]) + " /\nORDER\n";
    vector<uint16_t> B = p.start;
    int blank = (int)(find(B.begin(), B.end(), 0) - B.begin());
    for (uint8_t d : p.mv) {
        int nb = g.nbr[blank * 4 + d];
        out += to_string(B[nb]) + " " + TILE_CH[d] + "\n";
        B[blank] = B[nb]; B[nb] = 0; blank = nb;
    }
    out += "/\n";
    ofstream f(path + ".tmp"); f << out; f.close();
    rename((path + ".tmp").c_str(), path.c_str());
}

static vector<uint16_t> replay(const Geometry& g, vector<uint16_t> B, const vector<uint8_t>& mv) {
    int blank = (int)(find(B.begin(), B.end(), 0) - B.begin());
    for (uint8_t d : mv) {
        int nb = g.nbr[blank * 4 + d];
        if (nb < 0) die("replay: move leaves the board");
        B[blank] = B[nb]; B[nb] = 0; blank = nb;
    }
    return B;
}

// ---------------------------------------------------------------------------------------------
// Exact cycle removal: if the very same full state occurs twice, everything in between is dead weight.
// ---------------------------------------------------------------------------------------------
static inline u64 mix64(u64 x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}
static inline u64 zob(int cell, int tile) { return mix64(((u64)cell << 20) ^ (u64)tile); }

static long removeCycles(const Geometry& g, const vector<uint16_t>& start, vector<uint8_t>& mv) {
    vector<uint16_t> B = start;
    int blank = (int)(find(B.begin(), B.end(), 0) - B.begin());
    u64 H = 0;
    for (int c = 0; c < g.N; c++) H += zob(c, B[c]);
    size_t M = mv.size();
    vector<u64> hs(M + 1);
    hs[0] = H;
    for (size_t k = 0; k < M; k++) {
        int nb = g.nbr[blank * 4 + mv[k]], t = B[nb];
        H += zob(blank, t) + zob(nb, 0) - zob(nb, t) - zob(blank, 0);
        B[blank] = t; B[nb] = 0; blank = nb;
        hs[k + 1] = H;
    }
    unordered_map<u64, size_t> last;
    last.reserve(M * 2);
    for (size_t k = 0; k <= M; k++) last[hs[k]] = k;
    vector<uint8_t> out;
    for (size_t k = 0; k < M;) {
        size_t j = last[hs[k]];
        if (j > k) k = j;
        else { out.push_back(mv[k]); k++; }
    }
    long saved = (long)(M - out.size());
    mv.swap(out);
    return saved;
}

// ---------------------------------------------------------------------------------------------
// Window solver. Works on a private copy of the board at the current sweep position.
// ---------------------------------------------------------------------------------------------
struct Solver {
    const Geometry& g;
    vector<uint16_t> B;   // cell -> tile at the sweep position (temporarily modified by searches)
    vector<int> P;        // tile -> cell at the sweep position (window start)
    vector<int> Q;        // tile -> cell while replaying a window
    vector<int> tgt;      // tile -> target cell of the current window (== P for untouched tiles)
    vector<int> seen;
    vector<i64> prefW;    // prefW[L] = cost of the first L moves of the window scanned last
    int stamp = 0;
    int blank = 0;

    // search state (all costs are in COST units)
    i64 h = 0, bound = 0, nextBound = 0, pcost = 0;
    int mis = 0;          // tiles that are not yet on their target
    int plen = 0, depthCap = 0;
    long nodes = 0, budget = 0;
    bool aborted = false;
    uint8_t path[512];
    // randomised search
    bool randomize = false;
    u64 rng = 88172645463325252ULL;
    const uint8_t* avoid = nullptr;  // original window (an equal-cost rewrite must differ)
    int avoidLen = 0;

    explicit Solver(const Geometry& g_) : g(g_) {}

    void init(const vector<uint16_t>& board) {
        B = board;
        P.assign(g.N, 0); Q.assign(g.N, 0); tgt.assign(g.N, 0); seen.assign(g.N, 0); stamp = 0;
        for (int c = 0; c < g.N; c++) {
            if (B[c] == 0) blank = c;
            P[B[c]] = Q[B[c]] = tgt[B[c]] = c;
        }
    }
    inline u64 rnd() { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }

    // advance the sweep position by one move
    void commit(uint8_t d) {
        int nb = g.nbr[blank * 4 + d], t = B[nb];
        B[blank] = t; B[nb] = 0;
        P[t] = Q[t] = tgt[t] = blank;
        blank = nb;
    }
    // move without bookkeeping of P (used for scanning); undone by step(d^1)
    inline void step(uint8_t d) {
        int nb = g.nbr[blank * 4 + d], t = B[nb];
        B[blank] = t; B[nb] = 0; blank = nb;
    }

    // For every window length 1..maxL starting here: window cost minus the weighted Manhattan distance of the
    // displaced tiles (the slack). Pushes the lengths whose window could possibly be made cheaper.
    void scan(const uint8_t* m, int maxL, i64 maxSlack, vector<int>& cands) {
        cands.clear();
        prefW.assign(maxL + 1, 0);
        i64 hw = 0, W = 0;
        for (int k = 0; k < maxL; k++) {
            int nb = g.nbr[blank * 4 + m[k]], t = B[nb], T = P[t];
            hw += g.dhMove(m[k] ^ 1, nb, T);
            W += g.cb[m[k]];
            prefW[k + 1] = W;
            B[blank] = t; B[nb] = 0; blank = nb;
            i64 slack = W - hw;
            if (slack >= 1 && slack <= maxSlack) cands.push_back(k + 1);
        }
        for (int k = maxL - 1; k >= 0; k--) step(m[k] ^ 1);
    }

    // set tgt[] to the state reached after the L moves m[0..L), h to its weighted distance, mis to the number
    // of displaced tiles
    void prepareWindow(const uint8_t* m, int L, vector<int>& touched) {
        touched.clear(); ++stamp;
        for (int k = 0; k < L; k++) {
            int nb = g.nbr[blank * 4 + m[k]], t = B[nb];
            B[blank] = t; B[nb] = 0; Q[t] = blank; blank = nb;
            if (seen[t] != stamp) { seen[t] = stamp; touched.push_back(t); }
        }
        h = 0; mis = 0;
        for (int t : touched) { tgt[t] = Q[t]; h += g.wd(P[t], Q[t]); mis += (P[t] != Q[t]); }
        for (int k = L - 1; k >= 0; k--) {
            int nb = g.nbr[blank * 4 + (m[k] ^ 1)], t = B[nb];
            B[blank] = t; B[nb] = 0; Q[t] = blank; blank = nb;
        }
    }
    void releaseWindow(const vector<int>& touched) { for (int t : touched) tgt[t] = P[t]; }

    // gc = cost so far. Prunes with f = g + h > bound and remembers the smallest pruned f (next IDA* bound).
    bool dfs(i64 gc, int depth, int last) {
        if (mis == 0) {
            if (avoid && depth == avoidLen && memcmp(path, avoid, depth) == 0) return false;  // same as original
            plen = depth; pcost = gc;
            return true;
        }
        if (++nodes > budget) { aborted = true; return false; }
        if (depth >= depthCap) return false;   // only matters when some direction costs 0
        const int b = blank;
        int order[4] = {0, 1, 2, 3};
        if (randomize) {
            for (int i = 3; i > 0; i--) swap(order[i], order[rnd() % (i + 1)]);
        }
        for (int oi = 0; oi < 4; oi++) {
            int d = order[oi];
            if (d == (last ^ 1)) continue;      // never undo the previous move (costs >= 0)
            int nb = g.nbr[b * 4 + d];
            if (nb < 0) continue;
            int t = B[nb], T = tgt[t];
            i64 dh = g.dhMove(d ^ 1, nb, T);
            i64 mc = g.cb[d];
            i64 f = gc + mc + h + dh;
            if (f > bound) { if (f < nextBound) nextBound = f; continue; }
            int dm = (nb == T) - (b == T);
            B[b] = t; B[nb] = 0; blank = nb; h += dh; mis += dm; path[depth] = (uint8_t)d;
            bool ok = dfs(gc + mc, depth + 1, d);
            B[nb] = t; B[b] = 0; blank = b; h -= dh; mis -= dm;
            if (ok) return true;
            if (aborted) return false;
        }
        return false;
    }

    // random path of cost <= W (!= orig) from the window start to the window target. Returns its length or -1.
    int searchRandom(int L, i64 W, long budget_, const uint8_t* orig, vector<uint8_t>& out, i64& cost) {
        nodes = 0; budget = budget_; aborted = false; randomize = true;
        avoid = orig; avoidLen = L; bound = W; nextBound = LLONG_MAX; depthCap = min(2 * L + 2, 500);
        bool ok = dfs(0, 0, -1);
        randomize = false; avoid = nullptr;
        if (!ok) return -1;
        out.assign(path, path + plen);
        cost = pcost;
        return plen;
    }

    // exact search (IDA* on cost) for a path that costs less than W. Returns its length or -1.
    int searchShorter(int L, i64 W, long budget_, vector<uint8_t>& out, i64& cost) {
        nodes = 0; budget = budget_; aborted = false; randomize = false; avoid = nullptr;
        depthCap = min(2 * L + 2, 500);
        if (mis == 0) {                       // both ends are the same state: the whole window is a cycle
            if (W > 0) { out.clear(); cost = 0; return 0; }
            return -1;
        }
        for (bound = h; bound < W; bound = nextBound) {
            nextBound = LLONG_MAX;
            if (dfs(0, 0, -1)) { out.assign(path, path + plen); cost = pcost; return plen; }
            if (aborted || nextBound == LLONG_MAX) return -1;
        }
        return -1;
    }
};

// ---------------------------------------------------------------------------------------------
// Phase 1 on one chunk of the move list (the chunk is an independent problem: start board -> end board)
// ---------------------------------------------------------------------------------------------
struct Stats {
    atomic<long> windows{0}, success{0}, aborted{0}, exhausted{0}, nodes{0}, kicks{0}, kickWins{0};
};
static Stats ST;

struct Config {
    int Lmax = 24;
    int maxSlack = 1000;  // skip windows whose slack exceeds this many typical moves: search cost is ~2.1^(slack/2)
    long budget = 20000;
    double deadline = 1e18;
};

// Descent over start positions [lo, hi). S must be positioned at index lo. Windows are replaced in place.
// Returns the cost saved.
static i64 improveRange(Solver& S, vector<uint8_t>& mv, size_t lo, size_t hi, const Config& cfg) {
    i64 saved = 0;
    size_t i = lo;
    vector<int> cands, touched;
    vector<uint8_t> repl;
    const i64 maxSlack = cfg.maxSlack >= 1000 ? LLONG_MAX : (i64)cfg.maxSlack * S.g.unit;
    while (i < hi && i < mv.size()) {
        if (now_s() > cfg.deadline) break;
        int maxL = (int)min<size_t>(cfg.Lmax, mv.size() - i);
        S.scan(&mv[i], maxL, maxSlack, cands);
        bool improved = false;
        for (int L : cands) {
            i64 W = S.prefW[L], rc = 0;
            S.prepareWindow(&mv[i], L, touched);
            int res = S.searchShorter(L, W, cfg.budget, repl, rc);
            S.releaseWindow(touched);
            ST.windows++; ST.nodes += S.nodes;
            if (res >= 0) ST.success++; else if (S.aborted) ST.aborted++; else ST.exhausted++;
            if (res >= 0) {
                mv.erase(mv.begin() + i, mv.begin() + i + L);
                mv.insert(mv.begin() + i, repl.begin(), repl.end());
                saved += W - rc;
                hi = (size_t)max<long>(0, (long)hi + res - L);   // the replacement may be longer but cheaper
                improved = true;
                break;
            }
        }
        if (!improved) S.commit(mv[i++]);
    }
    return saved;
}

static void positionAt(Solver& S, const vector<uint16_t>& startBoard, const vector<uint8_t>& mv, size_t idx) {
    S.init(startBoard);
    for (size_t k = 0; k < idx; k++) S.commit(mv[k]);
}

static i64 improveChunk(Solver& S, vector<uint8_t>& mv, const Config& cfg) {
    return improveRange(S, mv, 0, mv.size(), cfg);
}

// Plateau phase on one chunk: rewrite a random window by a different path of the same cost, then
// re-run the descent around it. Neutral rewrites are always accepted, so the chunk performs a random walk
// over equal-cost solutions and any shortcut that becomes visible is taken immediately.
static i64 plateauChunk(Solver& S, const vector<uint16_t>& startBoard, vector<uint8_t>& mv, double endTime,
                        const Config& kcfg, int kmin, int kmax, long kickBudget) {
    i64 saved = 0;
    vector<int> touched;
    vector<uint8_t> repl;
    while (now_s() < endTime && mv.size() > (size_t)kmax + 2) {
        int Lw = kmin + (int)(S.rnd() % (u64)(kmax - kmin + 1));
        size_t i = (size_t)(S.rnd() % (u64)(mv.size() - Lw + 1));
        positionAt(S, startBoard, mv, i);
        i64 W = 0, rc = 0;
        for (int k = 0; k < Lw; k++) W += S.g.cb[mv[i + k]];
        S.prepareWindow(&mv[i], Lw, touched);
        int res = S.searchRandom(Lw, W, kickBudget, &mv[i], repl, rc);
        S.releaseWindow(touched);
        if (res < 0) continue;
        ST.kicks++;
        mv.erase(mv.begin() + i, mv.begin() + i + Lw);
        mv.insert(mv.begin() + i, repl.begin(), repl.end());
        saved += W - rc;
        size_t lo = i > (size_t)kcfg.Lmax ? i - kcfg.Lmax : 0;
        positionAt(S, startBoard, mv, lo);
        i64 gain = improveRange(S, mv, lo, i + res + 1, kcfg);
        if (gain) ST.kickWins++;
        saved += gain;
    }
    return saved;
}

// run one pass over the whole move list, split in independent chunks, in parallel
static i64 parallelPass(const Geometry& g, const Puzzle& p, vector<uint8_t>& mv, const Config& cfg,
                         int threads, size_t offset) {
    size_t M = mv.size();
    size_t chunkLen = max<size_t>(400, M / (size_t)(threads * 4));
    vector<size_t> cut;  // chunk boundaries
    cut.push_back(0);
    for (size_t x = (offset % chunkLen) ? offset % chunkLen : chunkLen; x < M; x += chunkLen) cut.push_back(x);
    cut.push_back(M);
    int nc = (int)cut.size() - 1;
    vector<vector<uint16_t>> boards(nc);
    {
        vector<uint16_t> B = p.start;
        int blank = (int)(find(B.begin(), B.end(), 0) - B.begin());
        size_t k = 0;
        for (int c = 0; c < nc; c++) {
            for (; k < cut[c]; k++) {
                int nb = g.nbr[blank * 4 + mv[k]];
                B[blank] = B[nb]; B[nb] = 0; blank = nb;
            }
            boards[c] = B;
        }
    }
    vector<vector<uint8_t>> res(nc);
    atomic<int> nextChunk(0);
    atomic<i64> total(0);
    auto worker = [&]() {
        Solver S(g);
        for (;;) {
            int c = nextChunk++;
            if (c >= nc) break;
            vector<uint8_t> sub(mv.begin() + cut[c], mv.begin() + cut[c + 1]);
            S.init(boards[c]);
            total += improveChunk(S, sub, cfg);
            res[c].swap(sub);
        }
    };
    vector<thread> th;
    for (int t = 1; t < threads; t++) th.emplace_back(worker);
    worker();
    for (auto& t : th) t.join();
    vector<uint8_t> out;
    for (int c = 0; c < nc; c++) out.insert(out.end(), res[c].begin(), res[c].end());
    mv.swap(out);
    return total;
}

// plateau slice: every chunk gets (slice * threads / chunks) seconds of random rewrites
static i64 plateauPass(const Geometry& g, const Puzzle& p, vector<uint8_t>& mv, double slice, int threads,
                        size_t offset, u64 seed, const Config& kcfg, int kmin, int kmax, long kickBudget) {
    size_t M = mv.size();
    size_t chunkLen = max<size_t>(600, M / (size_t)(threads * 2));
    vector<size_t> cut;
    cut.push_back(0);
    for (size_t x = (offset % chunkLen) ? offset % chunkLen : chunkLen; x + chunkLen / 2 < M; x += chunkLen) cut.push_back(x);
    cut.push_back(M);
    int nc = (int)cut.size() - 1;
    vector<vector<uint16_t>> boards(nc);
    {
        vector<uint16_t> B = p.start;
        int blank = (int)(find(B.begin(), B.end(), 0) - B.begin());
        size_t k = 0;
        for (int c = 0; c < nc; c++) {
            for (; k < cut[c]; k++) {
                int nb = g.nbr[blank * 4 + mv[k]];
                B[blank] = B[nb]; B[nb] = 0; blank = nb;
            }
            boards[c] = B;
        }
    }
    vector<vector<uint8_t>> res(nc);
    atomic<int> nextChunk(0);
    atomic<i64> total(0);
    double per = slice * threads / nc;
    auto worker = [&](int tid) {
        Solver S(g);
        for (;;) {
            int c = nextChunk++;
            if (c >= nc) break;
            vector<uint8_t> sub(mv.begin() + cut[c], mv.begin() + cut[c + 1]);
            S.init(boards[c]);
            S.rng = mix64(seed + 1000003ULL * (u64)c + (u64)tid) | 1;
            total += plateauChunk(S, boards[c], sub, now_s() + per, kcfg, kmin, kmax, kickBudget);
            res[c].swap(sub);
        }
    };
    vector<thread> th;
    for (int t = 1; t < threads; t++) th.emplace_back(worker, t);
    worker(0);
    for (auto& t : th) t.join();
    vector<uint8_t> out;
    for (int c = 0; c < nc; c++) out.insert(out.end(), res[c].begin(), res[c].end());
    mv.swap(out);
    return total;
}

// ---------------------------------------------------------------------------------------------
int main(int argc0, char** argv0) {
    bool keepFull = false;
    vector<char*> args;
    for (int i = 0; i < argc0; i++) { if (!strcmp(argv0[i], "--full")) keepFull = true; else args.push_back(argv0[i]); }
    int argc = (int)args.size();
    char** argv = args.data();
    if (argc < 3) {
        fprintf(stderr, "usage: %s in.txt out.txt [seconds=120] [threads=auto] [--full]\n"
                        "       seconds=0: only validate ORDER and print the objective\n"
                        "       --full:    never drop the tail of the order, even if that would raise F\n", argv[0]);
        return 1;
    }
    const string outPath = argv[2];
    double timeLimit = argc > 3 ? atof(argv[3]) : 120.0;
    int threads = argc > 4 ? atoi(argv[4]) : (int)thread::hardware_concurrency();
    if (threads < 1) threads = 1;

    Puzzle p;
    loadPuzzle(argv[1], p);
    Geometry g; g.init(p.R, p.C, p.cost);
    printf("board %dx%d (%d tiles), cost U/D/L/R = %lld/%lld/%lld/%lld\n", p.R, p.C, p.N - 1,
           p.cost[0], p.cost[1], p.cost[2], p.cost[3]);

    if (!p.hasOrder) {   // mode 1: DATA only
        if (!solvable(p)) {
            printf("NO SOLUTION: this starting position cannot be brought to the final position\n");
            return 4;
        }
        printf("The position is solvable, but the file has no ORDER block. This tool optimises an existing\n"
               "solution and cannot generate one.\n");
        return 5;
    }

    buildMoves(g, p);    // mode 2: DATA + ORDER, exits with a message naming the bad move
    const vector<uint16_t> goal = replay(g, p.start, p.mv);
    const i64 P = countPlaced(g, goal);
    auto F = [&](i64 cost) { return 100 * P - cost; };
    const i64 cost0 = pathCost(g, p.mv);
    printf("ORDER is valid: %zu moves, %lld of %d tiles in place, cost %lld, F = %lld\n", p.mv.size(), P, p.N - 1,
           cost0, F(cost0));
    if (timeLimit <= 0) return 0;
    printf("%d threads, %.0fs budget\n", threads, timeLimit);

    vector<uint8_t> mv = p.mv;
    auto verify = [&]() { if (replay(g, p.start, mv) != goal) die("BUG: final board changed"); };
    auto report = [&](const char* what) {
        verify();
        i64 c = pathCost(g, mv);
        printf("[%7.1fs] %-34s %zu moves, cost %lld (-%lld), F = %lld\n", now_s(), what, mv.size(), c, cost0 - c, F(c));
        fflush(stdout);
        Puzzle q = p; q.mv = mv; savePuzzle(outPath, q);
    };

    long s = removeCycles(g, p.start, mv);
    report(s ? "cycle removal" : "cycle removal (none)");

    if ((g.tc[0] | g.tc[1] | g.tc[2] | g.tc[3]) == 0) {
        printf("all four costs are zero: no move can be made cheaper, only the tail trimming below applies\n");
        timeLimit = 0;   // skips both phases
    }
    Config cfg;
    cfg.deadline = timeLimit;
    size_t offset = 0;
    auto descent = [&](int Lmax, long budget, int maxSlack = 1000) {
        cfg.Lmax = Lmax; cfg.budget = budget; cfg.maxSlack = maxSlack;
        parallelPass(g, p, mv, cfg, threads, offset);
        offset += 137;
        removeCycles(g, p.start, mv);
        char buf[96]; snprintf(buf, sizeof buf, "descent L<=%d budget=%ld%s", Lmax, budget, maxSlack < 1000 ? " (low slack)" : "");
        report(buf);
        ST.windows = ST.success = ST.aborted = ST.exhausted = ST.nodes = 0;
    };

    // phase 1: descent with growing windows / budgets
    descent(24, 20000);
    descent(32, 80000);
    descent(64, 5000, 8);      // long windows, but only the cheap (low-slack) ones
    descent(40, 320000);

    // phase 2: plateau walk + descent, repeated until the time is up
    Config kcfg;               // local descent that follows every plateau move
    kcfg.Lmax = 28; kcfg.budget = 20000; kcfg.deadline = 1e18;
    const int kmin = 10, kmax = 30;      // length of rewritten windows
    const long kickBudget = 3000;        // nodes for finding a different equal-cost path
    const double slice = 15;             // seconds per plateau round
    for (int round = 0; now_s() < timeLimit && mv.size() > (size_t)kmax + 2; round++) {
        double sl = min(slice, timeLimit - now_s());
        plateauPass(g, p, mv, sl, threads, offset, 12345 + round, kcfg, kmin, kmax, kickBudget);
        offset += 311;
        removeCycles(g, p.start, mv);
        char buf[96]; snprintf(buf, sizeof buf, "plateau round %d (%ld kicks, %ld improved)", round, ST.kicks.load(), ST.kickWins.load());
        report(buf);
        if (now_s() >= timeLimit) break;
        descent(32, 80000);
        if (round % 3 == 2 && now_s() < timeLimit) descent(40, 320000);
        if (round % 6 == 5 && now_s() < timeLimit) descent(48, 1280000);
    }

    // F is measured after the last move: keep the best prefix of the order
    size_t k = keepFull ? mv.size() : bestPrefix(g, p.start, mv);
    if (k < mv.size() && prefixScore(g, p.start, mv, k) > prefixScore(g, p.start, mv, mv.size())) {
        mv.resize(k);
        vector<uint16_t> fin = replay(g, p.start, mv);
        i64 c = pathCost(g, mv);
        printf("[%7.1fs] best prefix kept: %zu moves, %d tiles in place, cost %lld, F = %lld\n", now_s(), mv.size(),
               countPlaced(g, fin), c, 100LL * countPlaced(g, fin) - c);
        Puzzle q = p; q.mv = mv; savePuzzle(outPath, q);
    }
    return 0;
}