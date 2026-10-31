// io.cpp -- разбор входных файлов, запись решения, общая функция main.
#include "common.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

namespace p15 {

namespace {

struct Tok {
    std::string s;
    int line;
};

std::string upperStr(std::string s) {
    for (auto& ch : s) ch = (char)std::toupper((unsigned char)ch);
    return s;
}

bool toInt(const std::string& s, long long& v) {
    if (s.empty()) return false;
    size_t i = 0;
    bool neg = false;
    if (s[0] == '+' || s[0] == '-') {
        neg = s[0] == '-';
        i = 1;
        if (s.size() == 1) return false;
    }
    long long x = 0;
    for (; i < s.size(); i++) {
        if (!std::isdigit((unsigned char)s[i])) return false;
        x = x * 10 + (s[i] - '0');
        if (x > (1LL << 50)) return false;
    }
    v = neg ? -x : x;
    return true;
}

bool isKeyword(const std::string& s) {
    std::string u = upperStr(s);
    return u == "DATA" || u == "COST" || u == "ORDER";
}

// Разбивка на лексемы с номерами строк; "--" до конца строки -- комментарий;
// символ '/' всегда отдельная лексема.
std::vector<Tok> tokenize(const std::string& text) {
    std::vector<Tok> out;
    size_t i = 0, n = text.size();
    if (n >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB &&
        (unsigned char)text[2] == 0xBF)
        i = 3;   // BOM
    int line = 1;
    while (i <= n) {
        size_t e = text.find('\n', i);
        if (e == std::string::npos) e = n;
        std::string ln = text.substr(i, e - i);
        size_t cpos = ln.find("--");
        if (cpos != std::string::npos) ln.resize(cpos);
        std::string cur;
        for (char ch : ln) {
            if (ch == '/') {
                if (!cur.empty()) out.push_back({cur, line}), cur.clear();
                out.push_back({"/", line});
            } else if (std::isspace((unsigned char)ch)) {
                if (!cur.empty()) out.push_back({cur, line}), cur.clear();
            } else {
                cur += ch;
            }
        }
        if (!cur.empty()) out.push_back({cur, line});
        line++;
        if (e == n) break;
        i = e + 1;
    }
    return out;
}

bool validateCells(int R, int C, const std::vector<int>& cells, std::string& err) {
    int N = R * C, K = N - 1, blanks = 0;
    std::vector<int> cnt(N, 0);
    for (int v : cells) {
        if (v == 0) { blanks++; continue; }
        if (v < 1 || v > K) {
            err = "tile number " + std::to_string(v) + " is out of range 1.." + std::to_string(K);
            return false;
        }
        if (cnt[v]++) {
            err = "tile " + std::to_string(v) + " occurs more than once";
            return false;
        }
    }
    if (blanks != 1) {
        err = blanks == 0 ? "there is no empty cell ('=')" : "there is more than one empty cell ('=')";
        return false;
    }
    for (int v = 1; v <= K; v++)
        if (!cnt[v]) {
            err = "tile " + std::to_string(v) + " is missing";
            return false;
        }
    return true;
}

class VasekinParser {
public:
    VasekinParser(const std::vector<Tok>& t, Problem& p, std::string& e, bool s)
        : T(t), pr(p), err(e), strict(s) {}

    bool run() {
        bool haveData = false, haveCost = false;
        while (i < T.size()) {
            std::string u = upperStr(T[i].s);
            int ln = T[i].line;
            i++;
            if (u == "DATA") {
                if (haveData) return fail(ln, "duplicate DATA block");
                haveData = true;
                if (!parseData(ln)) return false;
            } else if (u == "COST") {
                if (haveCost) return fail(ln, "duplicate COST block");
                haveCost = true;
                if (!parseCost(ln)) return false;
            } else if (u == "ORDER") {
                if (pr.hasOrder) return fail(ln, "duplicate ORDER block");
                pr.hasOrder = true;
                if (!parseOrder(ln)) return false;
            } else if (u == "/") {
                if (strict) return fail(ln, "unexpected '/'");
                warn(ln, "unexpected '/' ignored");
            } else {
                return fail(ln, "unexpected token '" + T[i - 1].s + "' (DATA, COST or ORDER expected)");
            }
        }
        if (!haveData) {
            err = "no DATA block in the input";
            return false;
        }
        pr.costGiven = haveCost;
        return true;
    }

private:
    const std::vector<Tok>& T;
    size_t i = 0;
    Problem& pr;
    std::string& err;
    bool strict;

    bool fail(int line, const std::string& msg) {
        err = "line " + std::to_string(line) + ": " + msg;
        return false;
    }
    void warn(int line, const std::string& msg) {
        pr.warnings.push_back("line " + std::to_string(line) + ": " + msg);
    }
    int curLine(int def) const { return i < T.size() ? T[i].line : def; }

    bool terminator(int ln, const char* block) {
        if (i < T.size() && T[i].s == "/") { i++; return true; }
        std::string msg = std::string(block) + " block is not terminated by '/'";
        if (strict) return fail(curLine(ln), msg);
        warn(curLine(ln), msg);
        return true;
    }

    bool parseData(int dataLine) {
        if (i >= T.size() || upperStr(T[i].s) != "SIZE")
            return fail(curLine(dataLine), "SIZE expected after DATA");
        int sizeLine = T[i].line;
        i++;
        long long N = 0, M = 0;
        if (i + 1 >= T.size() || !toInt(T[i].s, N) || !toInt(T[i + 1].s, M))
            return fail(sizeLine, "SIZE must be followed by two integers: rows and columns");
        i += 2;
        if (N < 1 || M < 1) return fail(sizeLine, "board size must be positive");
        if (N * M > 1000000) return fail(sizeLine, "board is too large");
        if (N > 100 || M > 100) {
            if (strict) return fail(sizeLine, "rows and columns must not exceed 100");
            warn(sizeLine, "board is larger than 100 in some dimension");
        }
        if (i < T.size() && T[i].line == sizeLine && T[i].s != "/")
            return fail(sizeLine, "unexpected token '" + T[i].s + "' after SIZE");
        std::vector<int> cells;
        long long rows = 0;
        bool terminated = false;
        int lastLine = sizeLine;
        while (i < T.size()) {
            if (T[i].s == "/") { terminated = true; i++; break; }
            if (isKeyword(T[i].s)) break;
            int ln = T[i].line;
            lastLine = ln;
            std::vector<std::string> row;
            while (i < T.size() && T[i].line == ln && T[i].s != "/") row.push_back(T[i++].s);
            if (rows >= N)
                return fail(ln, "too many rows in DATA (SIZE gives " + std::to_string(N) + ")");
            if ((long long)row.size() != M)
                return fail(ln, "expected " + std::to_string(M) + " values in a row, found " +
                                    std::to_string(row.size()));
            for (auto& s : row) {
                long long v = 0;
                if (s == "=") {
                    cells.push_back(0);
                } else if (toInt(s, v)) {
                    if (v == 0) {
                        if (strict) return fail(ln, "'0' is not allowed, use '=' for the empty cell");
                        warn(ln, "'0' treated as the empty cell");
                    }
                    if (v < 0) return fail(ln, "negative tile number " + s);
                    cells.push_back((int)std::min<long long>(v, 2000000000LL));
                } else {
                    return fail(ln, "invalid value '" + s + "' (tile number or '=' expected)");
                }
            }
            rows++;
        }
        if (rows < N)
            return fail(lastLine, "not enough rows in DATA: expected " + std::to_string(N) +
                                      ", found " + std::to_string(rows));
        if (!terminated) {
            std::string msg = "DATA block is not terminated by '/'";
            if (strict) return fail(lastLine, msg);
            warn(lastLine, msg);
        }
        std::string e;
        if (!validateCells((int)N, (int)M, cells, e)) return fail(dataLine, "DATA: " + e);
        pr.R = (int)N;
        pr.C = (int)M;
        pr.start = cells;
        return true;
    }

    bool parseCost(int costLine) {
        std::vector<long long> v;
        while (i < T.size() && T[i].s != "/" && !isKeyword(T[i].s)) {
            long long x;
            if (!toInt(T[i].s, x)) return fail(T[i].line, "COST: integer expected, found '" + T[i].s + "'");
            v.push_back(x);
            i++;
        }
        if (!terminator(costLine, "COST")) return false;
        if (v.size() != 4) return fail(costLine, "COST: exactly four values expected (U D L R)");
        for (int d = 0; d < 4; d++) {
            if (v[d] < 0) return fail(costLine, "COST: costs must be non-negative");
            pr.cost[d] = v[d];
        }
        return true;
    }

    bool parseOrder(int orderLine) {
        while (i < T.size() && T[i].s != "/" && !isKeyword(T[i].s)) {
            long long t;
            int ln = T[i].line;
            if (!toInt(T[i].s, t)) return fail(ln, "ORDER: tile number expected, found '" + T[i].s + "'");
            i++;
            if (i >= T.size() || T[i].s == "/") return fail(ln, "ORDER: direction expected after tile number");
            std::string d = upperStr(T[i].s);
            int dir = d == "U" ? UP : d == "D" ? DOWN : d == "L" ? LEFT : d == "R" ? RIGHT : -1;
            if (dir < 0) return fail(T[i].line, "ORDER: direction U, D, L or R expected, found '" + T[i].s + "'");
            i++;
            pr.order.push_back({(int)std::max<long long>(-1, std::min<long long>(t, 2000000000LL)), dir});
            pr.orderLine.push_back(ln);
        }
        return terminator(orderLine, "ORDER");
    }
};

bool parseSimple(const std::vector<Tok>& T, Problem& pr, std::string& err) {
    std::vector<long long> v;
    for (auto& t : T) {
        long long x;
        if (!toInt(t.s, x)) {
            err = "line " + std::to_string(t.line) + ": integer expected, found '" + t.s + "'";
            return false;
        }
        v.push_back(x);
    }
    if (v.size() < 6) {
        err = "simplified format: at least 6 numbers expected (cU cD cL cR w h ...)";
        return false;
    }
    for (int d = 0; d < 4; d++) {
        if (v[d] < 0) { err = "costs must be non-negative"; return false; }
        pr.cost[d] = v[d];
    }
    long long w = v[4], h = v[5];
    if (w < 1 || h < 1 || w * h > 1000000) { err = "invalid board size"; return false; }
    if ((long long)v.size() != 6 + w * h) {
        err = "simplified format: expected " + std::to_string(w * h) + " cell values, found " +
              std::to_string((long long)v.size() - 6);
        return false;
    }
    if (w > 100 || h > 100) pr.warnings.push_back("board is larger than 100 in some dimension");
    std::vector<int> cells;
    for (long long k = 0; k < w * h; k++) {
        long long x = v[6 + k];
        if (x < 0 || x > 2000000000LL) { err = "invalid cell value " + std::to_string(x); return false; }
        cells.push_back((int)x);
    }
    if (!validateCells((int)h, (int)w, cells, err)) return false;
    pr.R = (int)h;
    pr.C = (int)w;
    pr.start = cells;
    pr.costGiven = true;
    return true;
}

}  // namespace

bool parseText(const std::string& text, Problem& pr, std::string& err, bool strict) {
    pr = Problem();
    std::vector<Tok> T = tokenize(text);
    bool vasekin = false;
    for (auto& t : T) {
        std::string u = upperStr(t.s);
        if (u == "DATA" || u == "COST" || u == "ORDER" || u == "SIZE" || u == "=" || u == "/") {
            vasekin = true;
            break;
        }
    }
    if (T.empty()) {
        err = "input is empty";
        return false;
    }
    if (vasekin) {
        pr.sourceFormat = "DATA";
        VasekinParser p(T, pr, err, strict);
        return p.run();
    }
    pr.sourceFormat = "simple";
    return parseSimple(T, pr, err);
}

bool parseFile(const std::string& path, Problem& pr, std::string& err, bool strict) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        err = "cannot open input file '" + path + "'";
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return parseText(ss.str(), pr, err, strict);
}

// ---------------------------------------------------------------- запись
namespace {

bool checkWritable(const std::string& path) {
    {
        std::ifstream in(path, std::ios::binary);
        if (in.good()) {
            in.close();
            std::ofstream out(path, std::ios::app | std::ios::binary);
            return out.good();
        }
    }
    std::ofstream out(path, std::ios::app | std::ios::binary);
    bool ok = out.good();
    out.close();
    if (ok) std::remove(path.c_str());
    return ok;
}

bool writeOutput(const std::string& path, const Problem& pr,
                 const std::vector<std::pair<int, int>>& tm,
                 const std::vector<std::string>& comments) {
    std::string s;
    s.reserve(256 + pr.start.size() * 6 + tm.size() * 8);
    for (auto& c : comments) s += "-- " + c + "\n";
    s += "DATA\nSIZE " + std::to_string(pr.R) + " " + std::to_string(pr.C) + "\n";
    for (int r = 0; r < pr.R; r++) {
        for (int c = 0; c < pr.C; c++) {
            if (c) s += ' ';
            int v = pr.start[r * pr.C + c];
            s += v == 0 ? std::string("=") : std::to_string(v);
        }
        s += '\n';
    }
    s += "/\nCOST\n";
    for (int d = 0; d < 4; d++) s += std::to_string(pr.cost[d]) + " ";
    s += "/\nORDER\n";
    for (auto& m : tm) {
        s += std::to_string(m.first);
        s += ' ';
        s += DIRCHAR[m.second];
        s += '\n';
    }
    s += "/\n";
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(s.data(), (std::streamsize)s.size());
    f.close();
    return !f.fail();
}

void writeErrorFile(const std::string& path, const std::string& msg) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (f) f << "-- ERROR: " << msg << "\n";
}

std::string unsolvableNote() {
    return "NO SOLUTION: the final arrangement is unreachable from this position "
           "(odd permutation); the ORDER below maximizes F anyway";
}

int verifyMode(const Problem& pr, const std::string& outPath) {
    Board b(pr);
    long long cost = 0;
    for (size_t i = 0; i < pr.order.size(); i++) {
        int t = pr.order[i].first, d = pr.order[i].second;
        std::string where = "move " + std::to_string(i + 1) + " (line " + std::to_string(pr.orderLine[i]) + ")";
        std::string bad;
        if (t < 1 || t > b.N - 1) {
            bad = where + ": there is no tile " + std::to_string(t);
        } else {
            int q = b.nb(b.pos[t], d);
            if (q < 0 || q != b.blank)
                bad = where + ": tile " + std::to_string(t) + " cannot move " + DIRCHAR[d];
        }
        if (!bad.empty()) {
            std::cerr << "ORDER error: " << bad << "\n";
            writeErrorFile(outPath, "ORDER is invalid: " + bad);
            return 6;
        }
        b.move(opposite(d));
        cost += pr.cost[d];
    }
    int P = b.placed();
    long long F = TILE_VALUE * P - cost;
    std::vector<std::string> com;
    com.push_back("ORDER verified: " + std::to_string(pr.order.size()) + " moves are valid");
    com.push_back("F = 100*P - sum(Cd*Nd) = 100*" + std::to_string(P) + " - " + std::to_string(cost) +
                  " = " + std::to_string(F));
    if (!goalReachable(pr)) com.push_back(unsolvableNote());
    if (!writeOutput(outPath, pr, pr.order, com)) {
        std::cerr << "error: cannot write '" << outPath << "'\n";
        return 3;
    }
    std::cout << F << std::endl;
    return 0;
}

void usage(const std::string& algo) {
    std::cerr << "usage: p15_" << algo << " <task file> <solution file> [options]\n"
              << "  --time T     time limit in seconds (anytime algorithms)\n"
              << "  --seed S     random seed\n"
              << "  --strict     strict input format checking\n"
              << "  -v           verbose log to stderr\n"
              << "  --key value  algorithm-specific parameters (see README)\n"
              << "Prints F to stdout and writes DATA/COST/ORDER to the solution file.\n";
}

}  // namespace

int runSolverMain(int argc, char** argv, const std::string& algo, double defaultTime, SolverFn fn) {
    Options opt;
    std::vector<std::string> pos;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-h" || a == "--help") { usage(algo); return 0; }
        if (a == "-v" || a == "--verbose") { opt.verbose++; continue; }
        if (a == "-vv") { opt.verbose += 2; continue; }
        if (a == "--strict") { opt.strict = true; continue; }
        if (a.size() > 1 && a[0] == '-' && !std::isdigit((unsigned char)a[1])) {
            std::string key = a.substr(a[1] == '-' ? 2 : 1), val;
            size_t eq = key.find('=');
            if (eq != std::string::npos) {
                val = key.substr(eq + 1);
                key = key.substr(0, eq);
            } else if (i + 1 < argc) {
                val = argv[++i];
            } else {
                std::cerr << "error: missing value for option " << a << "\n";
                return 1;
            }
            if (key == "t" || key == "time") opt.timeLimit = std::atof(val.c_str());
            else if (key == "seed") opt.seed = std::strtoull(val.c_str(), nullptr, 10);
            else opt.params[key] = val;
            continue;
        }
        pos.push_back(a);
    }
    if (pos.size() != 2) {
        usage(algo);
        return 1;
    }
    if (opt.timeLimit <= 0) opt.timeLimit = defaultTime;
    const std::string& inPath = pos[0];
    const std::string& outPath = pos[1];

    if (!checkWritable(outPath)) {
        std::cerr << "error: cannot write solution file '" << outPath << "'\n";
        return 3;
    }
    Problem pr;
    std::string err;
    if (!parseFile(inPath, pr, err, opt.strict)) {
        std::cerr << "input error: " << err << "\n";
        writeErrorFile(outPath, "input error: " + err);
        return 2;
    }
    for (auto& w : pr.warnings) std::cerr << "warning: " << w << "\n";
    if (pr.hasOrder) return verifyMode(pr, outPath);

    Timer timer(opt.timeLimit);
    Moves mv;
    try {
        if (pr.R == 1 || pr.C == 1) mv = solveLine(pr);
        else mv = fn(pr, opt, timer);
    } catch (const NotApplicable& e) {
        std::cerr << algo << ": not applicable: " << e.what() << "\n";
        return 5;
    } catch (const std::bad_alloc&) {
        std::cerr << algo << ": out of memory\n";
        return 4;
    } catch (const std::exception& e) {
        std::cerr << algo << ": internal error: " << e.what() << "\n";
        return 4;
    }
    Score sc;
    if (!simulate(pr, mv, sc)) {
        std::cerr << algo << ": internal error: illegal move sequence, using empty solution\n";
        mv.clear();
    }
    mv = polish(pr, mv);
    simulate(pr, mv, sc);

    // перевод ходов пустой клетки в ходы фишек
    std::vector<std::pair<int, int>> tm;
    tm.reserve(mv.size());
    {
        Board b(pr);
        for (uint8_t m : mv) {
            int t = b.move(m);
            tm.push_back({t, opposite(m)});
        }
    }
    bool reachable = goalReachable(pr);
    std::vector<std::string> com;
    char buf[256];
    std::snprintf(buf, sizeof buf, "algorithm: %s, time %.2f s", algo.c_str(), timer.elapsed());
    com.push_back(buf);
    com.push_back("F = 100*P - sum(Cd*Nd) = 100*" + std::to_string(sc.P) + " - " + std::to_string(sc.cost) +
                  " = " + std::to_string(sc.F));
    std::snprintf(buf, sizeof buf, "P = %d of %d tiles in place, %zu moves (U %lld, D %lld, L %lld, R %lld)",
                  sc.P, pr.tiles(), sc.moves, sc.cnt[0], sc.cnt[1], sc.cnt[2], sc.cnt[3]);
    com.push_back(buf);
    if (!reachable) {
        com.push_back(unsolvableNote());
        std::cerr << "UNSOLVABLE: the final arrangement is unreachable (odd permutation)\n";
    }
    if (!writeOutput(outPath, pr, tm, com)) {
        std::cerr << "error: cannot write solution file '" << outPath << "'\n";
        return 3;
    }
    logf(1, opt, "[%s] F=%lld P=%d/%d moves=%zu cost=%lld time=%.2fs\n", algo.c_str(), sc.F, sc.P,
         pr.tiles(), sc.moves, sc.cost, timer.elapsed());
    std::cout << sc.F << std::endl;
    return 0;
}

}  // namespace p15
