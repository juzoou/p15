// pdb.cpp -- построение аддитивных БД образцов обратным поиском (алгоритм Дейкстры
// с корзинами, ходы фишек вне группы стоят 0).
#include "pdb.hpp"

#include <algorithm>
#include <cstdlib>

namespace p15 {

void PatternDB::unrank(uint64_t idx, int* cells) const {
    uint64_t used = 0;
    for (int i = 0; i < k; i++) {
        int r = (int)((idx / mult[i]) % (uint64_t)(N - i));
        int c = 0;
        for (;; c++) {
            if ((used >> c) & 1) continue;
            if (r == 0) break;
            r--;
        }
        cells[i] = c;
        used |= 1ULL << c;
    }
}

// Обратный поиск от целевых расположений. Ходы фишек вне группы бесплатны,
// поэтому все положения пустой клетки внутри одной связной области
// (клетки без фишек группы) имеют одинаковое расстояние -- область
// обрабатывается целиком заливкой.
bool PatternDB::build(int R_, int C_, const std::vector<int>& tiles_, const std::vector<int>& goal_,
                      const long long cost[4], size_t maxStates) {
    R = R_;
    C = C_;
    N = R * C;
    tiles = tiles_;
    goal = goal_;
    k = (int)tiles.size();
    if (N > 64 || k < 1) return false;
    mult.assign(k, 1);
    for (int i = k - 2; i >= 0; i--) mult[i] = mult[i + 1] * (uint64_t)(N - i - 1);
    const uint64_t S = mult[0] * (uint64_t)N;   // число расположений группы
    if (S * (uint64_t)N > maxStates || S * (uint64_t)N >= 0x7FFFFFF0ULL) return false;
    long long maxc = 0;
    for (int d = 0; d < 4; d++) maxc = std::max(maxc, cost[d]);
    if (maxc > 4000) return false;

    const uint32_t INF = 0x7FFFFFFFu, DONE = 0x80000000u;
    std::vector<uint32_t> dist((size_t)(S * N), INF);   // старший бит -- "обработано"
    const int B = (int)maxc + 1;
    std::vector<std::vector<uint32_t>> bucket(B);
    size_t pending = 0;

    std::vector<int> cells(k);
    for (int i = 0; i < k; i++) cells[i] = goal[i];
    const uint64_t g0 = index(cells.data());
    std::vector<char> isGoal(N, 0);
    for (int i = 0; i < k; i++) isGoal[goal[i]] = 1;
    for (int b = 0; b < N; b++) {
        if (isGoal[b]) continue;
        uint32_t s = (uint32_t)(g0 * N + b);
        dist[s] = 0;
        bucket[0].push_back(s);
        pending++;
    }
    std::vector<int> at(N, -1), comp(N);
    std::vector<char> mark(N, 0);
    uint64_t cur = 0;
    while (pending > 0) {
        std::vector<uint32_t>& bk = bucket[cur % B];
        for (size_t qi = 0; qi < bk.size(); qi++) {
            uint32_t s = bk[qi];
            pending--;
            if (dist[s] != cur) continue;   // устарело или уже обработано (DONE)
            const uint64_t idx = s / N;
            unrank(idx, cells.data());
            for (int i = 0; i < k; i++) at[cells[i]] = i;
            // заливка области пустой клетки
            int nc = 0;
            comp[nc++] = (int)(s % N);
            mark[comp[0]] = 1;
            for (int h = 0; h < nc; h++) {
                int x = comp[h], xr = x / C, xc = x % C;
                for (int d = 0; d < 4; d++) {
                    int zr = xr + DROW[d], zc = xc + DCOL[d];
                    if (zr < 0 || zr >= R || zc < 0 || zc >= C) continue;
                    int z = zr * C + zc;
                    if (at[z] < 0 && !mark[z]) {
                        mark[z] = 1;
                        comp[nc++] = z;
                    }
                }
            }
            for (int h = 0; h < nc; h++) {
                mark[comp[h]] = 0;
                dist[(size_t)(idx * N + comp[h])] = (uint32_t)cur | DONE;
            }
            // ходы фишек группы: в прямом направлении фишка идёт из x в z
            for (int h = 0; h < nc; h++) {
                int x = comp[h], xr = x / C, xc = x % C;
                for (int d = 0; d < 4; d++) {
                    int zr = xr + DROW[d], zc = xc + DCOL[d];
                    if (zr < 0 || zr >= R || zc < 0 || zc >= C) continue;
                    int z = zr * C + zc;
                    int j = at[z];
                    if (j < 0) continue;
                    cells[j] = x;
                    uint64_t ns = index(cells.data()) * N + z;
                    cells[j] = z;
                    uint64_t nd = cur + (uint64_t)cost[d];
                    if (!(dist[ns] & DONE) && nd < dist[ns]) {
                        dist[ns] = (uint32_t)nd;
                        bucket[nd % B].push_back((uint32_t)ns);
                        pending++;
                    }
                }
            }
            for (int i = 0; i < k; i++) at[cells[i]] = -1;
        }
        bk.clear();
        cur++;
    }
    table.assign((size_t)S, 0);
    for (uint64_t idx = 0; idx < S; idx++) {
        uint32_t m = INF;
        const uint32_t* row = &dist[(size_t)(idx * N)];
        for (int b = 0; b < N; b++) m = std::min(m, row[b] & INF);
        table[idx] = m == INF ? 0 : m;
    }
    return true;
}

std::vector<std::vector<int>> makePartition(int R, int C, int k) {
    int K = R * C - 1;
    std::vector<char> used(K + 1, 0);
    std::vector<std::vector<int>> groups;
    auto cellOf = [&](int t) { return t - 1; };
    for (int seed = 1; seed <= K; seed++) {
        if (used[seed]) continue;
        std::vector<int> g{seed};
        used[seed] = 1;
        while ((int)g.size() < k) {
            int best = -1;
            long long bestKey = 0;
            for (int t = 1; t <= K; t++) {
                if (used[t]) continue;
                int r = cellOf(t) / C, c = cellOf(t) % C;
                int dmin = 1 << 30;
                for (int u : g) dmin = std::min(dmin, std::abs(r - cellOf(u) / C) + std::abs(c - cellOf(u) % C));
                int ds = std::abs(r - cellOf(seed) / C) + std::abs(c - cellOf(seed) % C);
                long long key = (long long)dmin * 1000000 + (long long)ds * 10000 + t;
                if (best < 0 || key < bestKey) { best = t; bestKey = key; }
            }
            if (best < 0) break;
            g.push_back(best);
            used[best] = 1;
        }
        groups.push_back(g);
    }
    return groups;
}

}  // namespace p15
