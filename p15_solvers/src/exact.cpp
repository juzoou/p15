// exact.cpp -- точный алгоритм: Дейкстра по всему пространству позиций.
//
// Ищется максимум F = 100*P(s) - d(start, s) по ВСЕМ достижимым позициям s,
// поэтому результат оптимален и для неразрешимых задач (частичная сборка),
// и для случаев, когда дособирать последние фишки невыгодно.
// Применим к полям, где число клеток N <= 10 (N!/2 <= 1.8 млн позиций).
#include <queue>

#include "solvers.hpp"

namespace p15 {

namespace {

uint64_t factorial(int n) {
    uint64_t f = 1;
    for (int i = 2; i <= n; i++) f *= (uint64_t)i;
    return f;
}

inline int popcount32(uint32_t x) {
    int c = 0;
    while (x) { x &= x - 1; c++; }
    return c;
}

struct PermCodec {
    int N;
    uint64_t fact[17];
    explicit PermCodec(int n) : N(n) {
        for (int i = 0; i <= 16; i++) fact[i] = factorial(i);
    }
    // позиция упакована по 4 бита на клетку
    static int get(uint64_t st, int i) { return (int)((st >> (4 * i)) & 15); }
    uint64_t rank(uint64_t st) const {
        uint64_t r = 0;
        uint32_t used = 0;
        for (int i = 0; i < N; i++) {
            int v = get(st, i);
            int less = popcount32(used & ((1u << v) - 1));
            r += (uint64_t)(v - less) * fact[N - 1 - i];
            used |= 1u << v;
        }
        return r;
    }
};

}  // namespace

Moves solveExact(const Problem& pr, const Options& opt, const Timer& tm) {
    const int R = pr.R, C = pr.C, N = R * C;
    const int maxCells = (int)opt.getI("exact-max-cells", 10);
    if (N > maxCells || N > 12)
        throw NotApplicable("board has " + std::to_string(N) + " cells, exact search supports at most " +
                            std::to_string(std::min(maxCells, 12)));
    PermCodec pc(N);
    const uint64_t S = pc.fact[N];
    const long long INF = (long long)4e18;
    std::vector<long long> dist(S, INF);
    std::vector<uint8_t> par(S, 255);

    uint64_t st0 = 0;
    for (int i = 0; i < N; i++) st0 |= (uint64_t)pr.start[i] << (4 * i);

    auto placedOf = [&](uint64_t st) {
        int p = 0;
        for (int i = 0; i + 1 < N; i++)
            if (PermCodec::get(st, i) == i + 1) p++;
        return p;
    };
    auto blankOf = [&](uint64_t st) {
        for (int i = 0; i < N; i++)
            if (PermCodec::get(st, i) == 0) return i;
        return -1;
    };

    const int K = N - 1;
    long long bestF = TILE_VALUE * placedOf(st0);
    uint64_t bestSt = st0;

    using QE = std::pair<long long, uint64_t>;
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> q;
    dist[pc.rank(st0)] = 0;
    q.push({0, st0});
    size_t popped = 0;
    while (!q.empty()) {
        auto [d, st] = q.top();
        q.pop();
        uint64_t r = pc.rank(st);
        if (d != dist[r]) continue;
        if (TILE_VALUE * K - d <= bestF) break;   // дальше улучшить F невозможно
        long long F = TILE_VALUE * placedOf(st) - d;
        if (F > bestF) { bestF = F; bestSt = st; }
        if ((++popped & 0xFFFF) == 0 && tm.expired()) break;
        int b = blankOf(st);
        int br = b / C, bc = b % C;
        for (int bd = 0; bd < 4; bd++) {
            int nr = br + DROW[bd], nc = bc + DCOL[bd];
            if (nr < 0 || nr >= R || nc < 0 || nc >= C) continue;
            int n = nr * C + nc;
            uint64_t v = (uint64_t)PermCodec::get(st, n);
            // фишка v переходит из n в b, пустая -- в n
            uint64_t ns = st & ~(15ULL << (4 * n)) & ~(15ULL << (4 * b));
            ns |= v << (4 * b);
            long long nd = d + pr.blankCost(bd);
            uint64_t r2 = pc.rank(ns);
            if (nd < dist[r2]) {
                dist[r2] = nd;
                par[r2] = (uint8_t)bd;
                q.push({nd, ns});
            }
        }
    }
    logf(1, opt, "[exact] states settled: %zu, best F = %lld\n", popped, bestF);

    // восстановление пути
    Moves mv;
    uint64_t st = bestSt;
    while (true) {
        uint8_t m = par[pc.rank(st)];
        if (m == 255) break;
        mv.push_back(m);
        int b = blankOf(st);                       // пустая пришла сюда ходом m
        int pb = b + (m == UP ? C : m == DOWN ? -C : m == LEFT ? 1 : -1);
        uint64_t v = (uint64_t)PermCodec::get(st, pb);
        uint64_t ns = st & ~(15ULL << (4 * pb)) & ~(15ULL << (4 * b));
        ns |= v << (4 * b);
        st = ns;
    }
    std::reverse(mv.begin(), mv.end());
    return mv;
}

}  // namespace p15
