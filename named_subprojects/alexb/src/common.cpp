// common.cpp -- поле, оценка решений, чётность, пост-обработка последовательностей.
#include "common.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

namespace p15 {

const int DROW[4] = {-1, 1, 0, 0};
const int DCOL[4] = {0, 0, -1, 1};
const char DIRCHAR[4] = {'U', 'D', 'L', 'R'};

Board::Board(int R_, int C_, const std::vector<int>& cells)
    : R(R_), C(C_), N(R_ * C_), a(cells), pos(R_ * C_, -1) {
    for (int i = 0; i < N; i++) pos[a[i]] = i;
    blank = pos[0];
}

int Board::placed() const {
    int p = 0;
    for (int i = 0; i + 1 < N; i++)
        if (a[i] == i + 1) p++;
    return p;
}

int dirBetween(int C, int u, int v) {
    if (v == u - C) return UP;
    if (v == u + C) return DOWN;
    if (v == u - 1) return LEFT;
    return RIGHT;
}

// ---------------------------------------------------------------- чётность
int parityClass(int R, int C, const std::vector<int>& cells) {
    int N = R * C;
    std::vector<char> seen(N, 0);
    int cycles = 0, blank = -1;
    for (int i = 0; i < N; i++) {
        if (cells[i] == 0) blank = i;
        if (seen[i]) continue;
        cycles++;
        int j = i;
        while (!seen[j]) {
            seen[j] = 1;
            int v = cells[j] == 0 ? N : cells[j];   // значение -> целевая клетка v-1
            j = v - 1;
        }
    }
    int permParity = (N - cycles) & 1;
    int dist = (R - 1 - blank / C) + (C - 1 - blank % C);
    return (permParity + dist) & 1;
}

bool goalReachable(int R, int C, const std::vector<int>& cells) {
    if (R == 1 || C == 1) {   // фишки на линии не могут менять порядок
        int prev = 0;
        for (int v : cells) {
            if (v == 0) continue;
            if (v < prev) return false;
            prev = v;
        }
        return true;
    }
    return parityClass(R, C, cells) == 0;
}

// ---------------------------------------------------------------- оценка
bool simulate(const Problem& pr, const Moves& mv, Score& sc, Board* fin) {
    Board b(pr);
    sc = Score();
    for (uint8_t m : mv) {
        if (m > 3 || !b.canMove(m)) return false;
        b.move(m);
        sc.cost += pr.blankCost(m);
        sc.cnt[m ^ 1]++;
    }
    sc.moves = mv.size();
    sc.P = b.placed();
    sc.F = TILE_VALUE * sc.P - sc.cost;
    if (fin) *fin = b;
    return true;
}

Score scoreOfEmpty(const Problem& pr) {
    Score sc;
    simulate(pr, Moves(), sc);
    return sc;
}

Moves bestPrefix(const Problem& pr, const Moves& mv) {
    Board b(pr);
    long long P = b.placed(), cost = 0;
    long long bestF = TILE_VALUE * P;
    size_t bestLen = 0;
    for (size_t i = 0; i < mv.size(); i++) {
        int from = b.nb(b.blank, mv[i]);
        if (from < 0) break;   // некорректный ход -- дальше не смотрим
        int to = b.blank;
        int t = b.a[from];
        if (from == t - 1) P--;
        if (to == t - 1) P++;
        b.move(mv[i]);
        cost += pr.blankCost(mv[i]);
        long long F = TILE_VALUE * P - cost;
        if (F > bestF) { bestF = F; bestLen = i + 1; }
    }
    return Moves(mv.begin(), mv.begin() + (long)bestLen);
}

// Удаление циклов: если позиция повторилась, всё между повторениями выкидывается.
// Конечная позиция при этом не меняется, стоимость не растёт.
static Moves removeLoops(const Problem& pr, const Moves& mv) {
    if (mv.size() > 30000000) return mv;
    Board b(pr);
    const uint64_t SALT = 0x5BD1E9955BD1E995ULL;
    uint64_t h1 = 0, h2 = 0;
    for (int c = 0; c < b.N; c++) {
        uint64_t k = zkey(c, b.a[c]);
        h1 ^= k;
        h2 ^= mix64(k ^ SALT);
    }
    std::unordered_map<uint64_t, uint32_t> seen;
    seen.reserve(mv.size() * 2 + 16);
    std::vector<uint64_t> H1{h1}, H2{h2};
    seen[h1] = 0;
    Moves out;
    out.reserve(mv.size());
    for (uint8_t m : mv) {
        int from = b.nb(b.blank, m), to = b.blank;
        if (from < 0) return mv;
        int t = b.a[from];
        auto upd = [&](int c, int tile) {
            uint64_t k = zkey(c, tile);
            h1 ^= k;
            h2 ^= mix64(k ^ SALT);
        };
        upd(from, t); upd(to, 0); upd(to, t); upd(from, 0);
        b.move(m);
        out.push_back(m);
        auto it = seen.find(h1);
        if (it != seen.end() && H2[it->second] == h2) {
            uint32_t j = it->second;   // позиция уже была после j ходов
            while (H1.size() > (size_t)j + 1) {
                seen.erase(H1.back());
                H1.pop_back();
                H2.pop_back();
            }
            out.resize(j);
        } else {
            seen[h1] = (uint32_t)out.size();
            H1.push_back(h1);
            H2.push_back(h2);
        }
    }
    return out;
}

Moves polish(const Problem& pr, const Moves& mv) {
    Moves a = bestPrefix(pr, mv);
    Moves b = removeLoops(pr, a);
    Score sa, sb;
    if (!simulate(pr, b, sb) || !simulate(pr, a, sa) || sb.F < sa.F) b = a;
    return bestPrefix(pr, b);
}

// ---------------------------------------------------------------- линия 1xM / Nx1
Moves solveLine(const Problem& pr) {
    Board b(pr);
    int L = b.N;
    int fwd = (pr.R == 1) ? RIGHT : DOWN;   // направление роста индекса клетки
    int bwd = opposite(fwd);
    long long bestF = TILE_VALUE * b.placed();
    int bestK = b.blank;
    for (int k = 0; k < L; k++) {
        if (k == b.blank) continue;
        Board t = b;
        long long cost = 0;
        int d = (k > b.blank) ? fwd : bwd;
        while (t.blank != k) {
            t.move(d);
            cost += pr.blankCost(d);
        }
        long long F = TILE_VALUE * t.placed() - cost;
        if (F > bestF) { bestF = F; bestK = k; }
    }
    Moves mv;
    int d = bestK > b.blank ? fwd : bwd;
    for (int i = 0; i < std::abs(bestK - b.blank); i++) mv.push_back((uint8_t)d);
    return mv;
}

// ---------------------------------------------------------------- опции, лог
double Options::getD(const std::string& k, double def) const {
    auto it = params.find(k);
    if (it == params.end()) return def;
    return std::atof(it->second.c_str());
}
long long Options::getI(const std::string& k, long long def) const {
    auto it = params.find(k);
    if (it == params.end()) return def;
    return std::atoll(it->second.c_str());
}

void logf(int level, const Options& opt, const char* fmt, ...) {
    if (opt.verbose < level) return;
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
    std::fflush(stderr);
}

}  // namespace p15
