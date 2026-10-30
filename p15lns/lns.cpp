// lns.cpp -- послойная сборка с точной (по стоимости) проводкой фишек + LNS по
// последовательности ходов (поиск более дешёвых обходов между состояниями пути).
#include "lns.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <queue>
#include <tuple>
#include <unordered_map>

namespace {

typedef long long i64;
typedef uint64_t u64;
typedef uint32_t u32;
typedef uint8_t u8;
typedef std::vector<u8> Moves;  // ходы ПУСТОЙ клетки: 0=U 1=D 2=L 3=R

const int DR[4] = {-1, 1, 0, 0};
const int DC[4] = {0, 0, -1, 1};
const char DCH[4] = {'U', 'D', 'L', 'R'};
const i64 TV = 100;
const i64 INF = (i64)4e18;
inline int opp(int d) { return d ^ 1; }

struct Clock {
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    double limit = 30;
    double elapsed() const { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); }
    bool expired() const { return elapsed() >= limit; }
    double remaining() const { return limit - elapsed(); }
};

inline u64 mix64(u64 x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}
inline u64 zk(int cell, int tile) { return mix64(((u64)cell << 20) ^ (u64)tile ^ 0xA5A5F00DULL); }

struct Ctx {
    int R, C, N;
    i64 w[4];  // цена хода фишки U D L R
    std::vector<int> a0;
    i64 bcost(int bd) const { return w[bd ^ 1]; }  // ход пустой клетки bd = ход фишки в обратную сторону
    int nb(int cell, int d) const {
        int r = cell / C + DR[d], c = cell % C + DC[d];
        if (r < 0 || r >= R || c < 0 || c >= C) return -1;
        return r * C + c;
    }
    i64 wmd(int from, int to) const {  // взвешенное манхэттенское расстояние фишки
        int dr = to / C - from / C, dc = to % C - from % C;
        return (dr > 0 ? dr * w[1] : -dr * w[0]) + (dc > 0 ? dc * w[3] : -dc * w[2]);
    }
};

// ---------------------------------------------------------------------------------
// оценка / полировка
// ---------------------------------------------------------------------------------
void evalMoves(const Ctx& X, const Moves& mv, i64& F, i64& cost, int& P, bool& ok) {
    std::vector<int> a = X.a0;
    int b = (int)(std::find(a.begin(), a.end(), 0) - a.begin());
    cost = 0;
    ok = true;
    for (u8 m : mv) {
        int n = X.nb(b, m);
        if (n < 0) { ok = false; return; }
        a[b] = a[n];
        a[n] = 0;
        b = n;
        cost += X.bcost(m);
    }
    P = 0;
    for (int i = 0; i < X.N - 1; i++) P += a[i] == i + 1;
    F = TV * P - cost;
}

// убрать циклы (повторы позиций) и обрезать до префикса с максимальным F
Moves polish(const Ctx& X, const Moves& mv) {
    std::vector<int> a = X.a0;
    int N = X.N;
    int b = (int)(std::find(a.begin(), a.end(), 0) - a.begin());
    u64 h = 0;
    for (int i = 0; i < N; i++) h ^= zk(i, a[i]);
    std::unordered_map<u64, int> seen;
    seen.reserve(mv.size() * 2 + 16);
    Moves kept;
    std::vector<u64> hashes{h};
    seen[h] = 0;
    for (u8 m : mv) {
        int n = X.nb(b, m);
        int t = a[n];
        h ^= zk(b, 0) ^ zk(n, t) ^ zk(b, t) ^ zk(n, 0);
        a[b] = t;
        a[n] = 0;
        b = n;
        auto it = seen.find(h);
        if (it != seen.end()) {
            int k = it->second;
            for (size_t j = k + 1; j < hashes.size(); j++) seen.erase(hashes[j]);
            hashes.resize(k + 1);
            kept.resize(k);
        } else {
            kept.push_back(m);
            hashes.push_back(h);
            seen[h] = (int)kept.size();
        }
    }
    a = X.a0;
    b = (int)(std::find(a.begin(), a.end(), 0) - a.begin());
    int P = 0;
    for (int i = 0; i < N - 1; i++) P += a[i] == i + 1;
    i64 cost = 0, bestF = TV * P;
    size_t bestLen = 0;
    for (size_t k = 0; k < kept.size(); k++) {
        int n = X.nb(b, kept[k]);
        int t = a[n];
        P += (b == t - 1) - (n == t - 1);
        a[b] = t;
        a[n] = 0;
        b = n;
        cost += X.bcost(kept[k]);
        if (TV * P - cost > bestF) { bestF = TV * P - cost; bestLen = k + 1; }
    }
    kept.resize(bestLen);
    return kept;
}

// поля 1xM / Nx1: порядок фишек неизменен, выбираем, где остановить пустую клетку
Moves solveLine(const Ctx& X) {
    int b0 = (int)(std::find(X.a0.begin(), X.a0.end(), 0) - X.a0.begin());
    std::vector<int> tiles;
    for (int v : X.a0) if (v) tiles.push_back(v);
    bool horiz = X.R == 1;
    i64 bestF = INT64_MIN;
    int bestB = b0;
    for (int b = 0; b < X.N; b++) {
        int P = 0, k = 0;
        for (int c = 0; c < X.N; c++) if (c != b) P += tiles[k++] == c + 1;
        i64 cost = 0;
        int dir = b > b0 ? (horiz ? 3 : 1) : (horiz ? 2 : 0);
        cost = (i64)std::abs(b - b0) * X.bcost(dir);
        if (TV * P - cost > bestF) { bestF = TV * P - cost; bestB = b; }
    }
    int dir = bestB > b0 ? (horiz ? 3 : 1) : (horiz ? 2 : 0);
    return Moves((size_t)std::abs(bestB - b0), (u8)dir);
}

// ---------------------------------------------------------------------------------
// послойная сборка
// ---------------------------------------------------------------------------------
class Builder {
public:
    bool sacrifice = false;  // не тратиться на фишки, проводка которых дороже их ценности
    i64 sacThr = 100;
    int lamPct = 0;  // потенциальная поправка рёбер пути пустой клетки (0..95 %)
    Builder(const Ctx& x, const Clock& clk) : X(x), clk(clk), R(x.R), C(x.C), N(x.N) {
        a = x.a0;
        pos.assign(N, 0);
        for (int c = 0; c < N; c++) pos[a[c]] = c;
        blank = pos[0];
        locked.assign(N, 0);
        dist.assign(N, 0); pdir.assign(N, -1); dstamp.assign(N, 0); settled.assign(N, 0);
        gs.assign(4 * (size_t)N, 0); gfrom.assign(4 * (size_t)N, -1); gstamp.assign(4 * (size_t)N, 0);
    }

    Moves build() {
        int top = R, bot = -1, left = C, right = -1;
        auto touch = [&](int c) {
            top = std::min(top, c / C); bot = std::max(bot, c / C);
            left = std::min(left, c % C); right = std::max(right, c % C);
        };
        for (int c = 0; c < N; c++) {
            int goal = c < N - 1 ? c + 1 : 0;
            if (a[c] != goal) touch(c);
        }
        if (bot < 0) return out;
        if (N <= 10) { exactFinish(0, R - 1, 0, C - 1); return out; }  // малые поля: точно
        touch(blank);
        while (bot - top < 1) { if (bot + 1 < R) bot++; else top--; }
        while (right - left < 1) { if (right + 1 < C) right++; else left--; }
        for (int c = 0; c < N; c++) {
            int r = c / C, cc = c % C;
            if (r < top || r > bot || cc < left || cc > right) locked[c] = 1;
        }
        while (!clk.expired()) {
            int rows = bot - top + 1, cols = right - left + 1;
            std::vector<int> L;
            int ir, ic;
            bool rowStage;
            if (rows > 3 && (rows >= cols || cols <= 3)) {
                for (int c = left; c <= right; c++) L.push_back(top * C + c);
                ir = 1; ic = 0; rowStage = true;
            } else if (cols > 3) {
                for (int r = top; r <= bot; r++) L.push_back(r * C + left);
                ir = 0; ic = 1; rowStage = false;
            } else {
                exactFinish(top, bot, left, right);
                return out;
            }
            if (!placeLine(L, ir * C + ic)) return out;
            if (rowStage) top++; else left++;
        }
        return out;
    }

private:
    const Ctx& X;
    const Clock& clk;
    int R, C, N;
    std::vector<int> a, pos;
    int blank;
    std::vector<char> locked;
    Moves out;

    // --- Дейкстра пустой клетки
    std::vector<i64> dist;
    std::vector<int> pdir;
    std::vector<u32> dstamp, settled;
    u32 dgen = 0;

    // цена ребра пути пустой клетки u -> v (ход bd) с поправкой на сдвиг чужой фишки a[v] в клетку u
    i64 edge(int u, int v, int bd) const {
        i64 c = 100 * X.bcost(bd);  // внутренний масштаб x100
        if (lamPct) {
            int t = a[v];
            if (t > 0) c += (X.wmd(u, t - 1) - X.wmd(v, t - 1)) * lamPct;
        }
        return c;
    }
    void doMove(int bd) {
        int n = X.nb(blank, bd), t = a[n];
        a[blank] = t; pos[t] = blank;
        a[n] = 0; pos[0] = n;
        blank = n;
        out.push_back((u8)bd);
    }

    void blankDijkstra(int avoid, const int* tg, int nt) {
        dgen++;
        typedef std::pair<i64, int> QE;
        std::priority_queue<QE, std::vector<QE>, std::greater<QE>> pq;
        dist[blank] = 0; dstamp[blank] = dgen; pdir[blank] = -1;
        pq.push({0, blank});
        int rem = nt;
        while (!pq.empty()) {
            QE e = pq.top(); pq.pop();
            int u = e.second;
            if (settled[u] == dgen || e.first != dist[u]) continue;
            settled[u] = dgen;
            for (int k = 0; k < nt; k++) if (tg[k] == u) rem--;
            if (nt > 0 && rem <= 0) break;
            for (int bd = 0; bd < 4; bd++) {
                int v = X.nb(u, bd);
                if (v < 0 || locked[v] || v == avoid) continue;
                i64 nd = e.first + edge(u, v, bd);
                if (dstamp[v] != dgen || nd < dist[v]) { dist[v] = nd; dstamp[v] = dgen; pdir[v] = bd; pq.push({nd, v}); }
            }
        }
    }
    i64 distTo(int cell) const { return settled[cell] == dgen ? dist[cell] : -1; }
    void pathTo(int cell, Moves& mv) const {
        Moves rev;
        int u = cell;
        while (pdir[u] >= 0) { rev.push_back((u8)pdir[u]); u = X.nb(u, opp(pdir[u])); }
        mv.insert(mv.end(), rev.rbegin(), rev.rend());
    }

    // --- мини-Дейкстра вокруг клетки center в окне 5x5
    i64 mdist[25]; int mpar[25], mprev[25]; char mdone[25];
    int widx(int center, int cell) const {
        int r = cell / C - center / C + 2, c = cell % C - center % C + 2;
        return (r < 0 || r > 4 || c < 0 || c > 4) ? -1 : r * 5 + c;
    }
    void mini(int center, int from) {
        int cr = center / C, cc = center % C;
        for (int i = 0; i < 25; i++) { mdist[i] = INF; mdone[i] = 0; mpar[i] = -1; mprev[i] = -1; }
        mdist[widx(center, from)] = 0;
        for (;;) {
            int bi = -1;
            for (int i = 0; i < 25; i++) if (!mdone[i] && mdist[i] < INF && (bi < 0 || mdist[i] < mdist[bi])) bi = i;
            if (bi < 0) break;
            mdone[bi] = 1;
            int u = (cr + bi / 5 - 2) * C + cc + bi % 5 - 2;
            for (int bd = 0; bd < 4; bd++) {
                int v = X.nb(u, bd);
                if (v < 0 || v == center || locked[v]) continue;
                int j = widx(center, v);
                if (j < 0 || mdone[j]) continue;
                i64 nd = mdist[bi] + edge(u, v, bd);
                if (nd < mdist[j]) { mdist[j] = nd; mpar[j] = bd; mprev[j] = bi; }
            }
        }
    }
    void miniPath(int center, int to, Moves& mv) const {
        Moves rev;
        int j = widx(center, to);
        while (mpar[j] >= 0) { rev.push_back((u8)mpar[j]); j = mprev[j]; }
        mv.insert(mv.end(), rev.rbegin(), rev.rend());
    }

    // --- A* по состояниям (клетка фишки, сторона пустой клетки)
    std::vector<i64> gs;
    std::vector<int> gfrom;
    std::vector<u32> gstamp;
    u32 agen = 0;

    // стоимость проводки фишки t в клетку goal; apply -- выполнить ходы. -1 -- невозможно
    i64 route(int t, int goal, bool apply, i64 limit = -1) {
        int p0 = pos[t];
        if (p0 == goal) return 0;
        if (locked[p0] || locked[goal]) return -1;
        agen++;
        int tg[4], nt = 0, sidec[4];
        for (int s = 0; s < 4; s++) {
            int c = X.nb(p0, s);
            sidec[s] = (c >= 0 && !locked[c]) ? c : -1;
            if (sidec[s] >= 0) tg[nt++] = c;
        }
        if (!nt) return -1;
        blankDijkstra(p0, tg, nt);
        typedef std::tuple<i64, i64, int> QE;
        std::priority_queue<QE, std::vector<QE>, std::greater<QE>> pq;
        for (int s = 0; s < 4; s++) {
            if (sidec[s] < 0) continue;
            i64 d = distTo(sidec[s]);
            if (d < 0) continue;
            int st = p0 * 4 + s;
            gs[st] = d; gfrom[st] = -1; gstamp[st] = agen;
            pq.push(QE(d + 100 * X.wmd(p0, goal), d, st));
        }
        int fin = -1;
        i64 total = -1;
        auto relax = [&](int nst, i64 ng, int from) {
            if (gstamp[nst] != agen || ng < gs[nst]) {
                gs[nst] = ng; gfrom[nst] = from; gstamp[nst] = agen;
                pq.push(QE(ng + 100 * X.wmd(nst >> 2, goal), ng, nst));
            }
        };
        while (!pq.empty()) {
            QE e = pq.top(); pq.pop();
            i64 g = std::get<1>(e);
            int st = std::get<2>(e), p = st >> 2, s = st & 3;
            if (g != gs[st]) continue;
            if (p == goal) { fin = st; total = g; break; }
            if (limit >= 0 && std::get<0>(e) > limit) break;
            int c1 = X.nb(p, s);
            relax(c1 * 4 + opp(s), g + 100 * X.w[s], st);  // толчок: фишка уходит в клетку c1
            mini(p, c1);
            for (int s2 = 0; s2 < 4; s2++) {
                if (s2 == s) continue;
                int c2 = X.nb(p, s2);
                if (c2 < 0 || locked[c2]) continue;
                i64 d = mdist[widx(p, c2)];
                if (d < INF) relax(p * 4 + s2, g + d, st);
            }
        }
        if (fin < 0) return -1;
        if (!apply) return total;
        std::vector<int> chain;
        for (int st = fin; st >= 0; st = gfrom[st]) chain.push_back(st);
        std::reverse(chain.begin(), chain.end());
        {
            int c = X.nb(p0, chain[0] & 3);
            blankDijkstra(p0, &c, 1);
            Moves mv;
            pathTo(c, mv);
            for (u8 m : mv) doMove(m);
        }
        for (size_t i = 0; i + 1 < chain.size(); i++) {
            int p = chain[i] >> 2, s = chain[i] & 3, np = chain[i + 1] >> 2;
            if (np != p) doMove(opp(s));
            else {
                int c1 = X.nb(p, s), c2 = X.nb(p, chain[i + 1] & 3);
                mini(p, c1);
                Moves mv;
                miniPath(p, c2, mv);
                for (u8 m : mv) doMove(m);
            }
        }
        return total;
    }

    // --- линия и пара последних клеток
    // оценка реальной стоимости проводки (без поправки), -1 если дороже limit/невозможно
    i64 estimate(int t, int goal, i64 limit) {
        int sv = lamPct;
        lamPct = 0;
        i64 c = route(t, goal, false, limit * 100);
        lamPct = sv;
        return c < 0 ? -1 : c / 100;
    }
    bool giveUp(int g) {  // оставить клетку как есть и заблокировать
        if (blank == g) {
            int bestBd = -1;
            for (int bd = 0; bd < 4; bd++) {
                int v = X.nb(g, bd);
                if (v >= 0 && !locked[v] && (bestBd < 0 || X.bcost(bd) < X.bcost(bestBd))) bestBd = bd;
            }
            if (bestBd < 0) return false;
            doMove(bestBd);
        }
        locked[g] = 1;
        return true;
    }
    bool placeLine(const std::vector<int>& L, int off) {
        int m = (int)L.size();
        for (int k = 0; k + 2 < m; k++) {
            if (clk.expired()) return false;
            int g = L[k];
            if (a[g] != g + 1) {
                if (sacrifice && estimate(g + 1, g, sacThr) < 0) { if (!giveUp(g)) return false; continue; }
                if (route(g + 1, g, true) < 0) {
                    if (sacrifice) { if (!giveUp(g)) return false; continue; }
                    return false;
                }
            }
            locked[g] = 1;
        }
        int A = L[m - 2], B = L[m - 1], ta = A + 1, tb = B + 1;
        if (!(a[A] == ta && a[B] == tb)) {
            if (sacrifice) {
                bool lost = locked[pos[ta]] || locked[pos[tb]] || estimate(ta, B, 2 * sacThr) < 0;
                if (lost) return giveUp(A) && giveUp(B);
            }
            if (route(ta, B, true) < 0) return false;
            locked[B] = 1;
            bool ok = route(tb, B + off, true) >= 0;
            locked[B] = 0;
            if (!ok) {  // b стоит так, что пустая клетка запирается: уводим b вглубь и повторяем
                int deep = B + 2 * off;
                if (route(tb, deep, true) < 0) return false;
                locked[deep] = 1;
                bool ok1 = route(ta, B, true) >= 0;
                locked[deep] = 0;
                if (!ok1) return false;
                locked[B] = 1;
                ok = route(tb, B + off, true) >= 0;
                locked[B] = 0;
            }
            if (!ok) return false;
            if (!windowFinish(A, B, off, ta, tb)) return false;
        }
        locked[A] = locked[B] = 1;
        return true;
    }

    bool windowFinish(int A, int B, int off, int ta, int tb) {
        int W[6] = {A, B, A + off, B + off, A + 2 * off, B + 2 * off};
        auto wi = [&](int cell) { for (int i = 0; i < 6; i++) if (W[i] == cell) return i; return -1; };
        if (wi(blank) < 0) {
            int ca = pos[ta], cb = pos[tb];
            locked[ca] = locked[cb] = 1;
            int tg[6], nt = 0;
            for (int i = 0; i < 6; i++) if (W[i] != ca && W[i] != cb) tg[nt++] = W[i];
            blankDijkstra(-1, tg, nt);
            int best = -1;
            for (int k = 0; k < nt; k++) { i64 d = distTo(tg[k]); if (d >= 0 && (best < 0 || d < distTo(best))) best = tg[k]; }
            Moves mv;
            if (best >= 0) pathTo(best, mv);
            locked[ca] = locked[cb] = 0;
            if (best < 0) return false;
            for (u8 m : mv) doMove(m);
        }
        int ia = wi(pos[ta]), ib = wi(pos[tb]), bl = wi(blank);
        if (ia < 0 || ib < 0 || bl < 0) return false;
        i64 d[216]; int par[216], pmv[216]; char done[216];
        for (int i = 0; i < 216; i++) { d[i] = INF; done[i] = 0; par[i] = -1; }
        int s0 = (ia * 6 + ib) * 6 + bl;
        d[s0] = 0;
        int goalS = -1;
        for (;;) {
            int bi = -1;
            for (int i = 0; i < 216; i++) if (!done[i] && d[i] < INF && (bi < 0 || d[i] < d[bi])) bi = i;
            if (bi < 0) break;
            done[bi] = 1;
            int xa = bi / 36, xb = (bi / 6) % 6, xl = bi % 6;
            if (xa == 0 && xb == 1) { goalS = bi; break; }
            for (int bd = 0; bd < 4; bd++) {
                int v = X.nb(W[xl], bd);
                int j = v < 0 ? -1 : wi(v);
                if (j < 0) continue;
                int na = xa, nb2 = xb;
                if (j == xa) na = xl;
                if (j == xb) nb2 = xl;
                int ns = (na * 6 + nb2) * 6 + j;
                i64 nd = d[bi] + X.bcost(bd);
                if (nd < d[ns]) { d[ns] = nd; par[ns] = bi; pmv[ns] = bd; }
            }
        }
        if (goalS < 0) return false;
        Moves mv;
        for (int s = goalS; par[s] >= 0; s = par[s]) mv.push_back((u8)pmv[s]);
        std::reverse(mv.begin(), mv.end());
        for (u8 m : mv) doMove(m);
        return true;
    }

    // --- точный хвост (регион <= 3x3): Дейкстра с максимизацией F
    void exactFinish(int top, int bot, int left, int right) {
        std::vector<int> RC, loc(N, -1);
        for (int r = top; r <= bot; r++)
            for (int c = left; c <= right; c++) { loc[r * C + c] = (int)RC.size(); RC.push_back(r * C + c); }
        int k = (int)RC.size();
        if (k < 2 || k > 10) return;
        std::vector<std::array<int, 4>> adj(k);
        for (int i = 0; i < k; i++)
            for (int d = 0; d < 4; d++) { int v = X.nb(RC[i], d); adj[i][d] = v < 0 ? -1 : loc[v]; }
        u64 start = 0;
        for (int i = 0; i < k; i++) {
            int t = a[RC[i]];
            u64 lab = t == 0 ? 15 : (loc[t - 1] >= 0 ? (u64)loc[t - 1] : 14);
            start |= lab << (4 * i);
        }
        struct Info { i64 d; u64 par; };
        std::unordered_map<u64, Info> mp;
        mp.reserve(k > 9 ? 2000000 : 500000);
        typedef std::pair<i64, u64> QE;
        std::priority_queue<QE, std::vector<QE>, std::greater<QE>> pq;
        mp[start] = {0, ~0ULL};
        pq.push({0, start});
        i64 bestF = INT64_MIN, bestD = 0;
        u64 bestKey = start;
        while (!pq.empty()) {
            QE e = pq.top(); pq.pop();
            if (mp[e.second].d != e.first) continue;
            u64 key = e.second;
            int P = 0, bl = -1;
            for (int i = 0; i < k; i++) {
                int lab = (int)((key >> (4 * i)) & 15);
                if (lab == 15) bl = i;
                else if (lab == i) P++;
            }
            i64 F = TV * P - e.first;
            if (F > bestF || (F == bestF && e.first < bestD)) { bestF = F; bestD = e.first; bestKey = key; }
            for (int d = 0; d < 4; d++) {
                int j = adj[bl][d];
                if (j < 0) continue;
                u64 lab = (key >> (4 * j)) & 15;
                u64 nk = (key & ~(15ULL << (4 * j)) & ~(15ULL << (4 * bl))) | (15ULL << (4 * j)) | (lab << (4 * bl));
                i64 nd = e.first + X.bcost(d);
                auto it = mp.find(nk);
                if (it == mp.end() || nd < it->second.d) { mp[nk] = {nd, key}; pq.push({nd, nk}); }
            }
        }
        // восстановление ходов по цепочке состояний
        std::vector<u64> chain;
        for (u64 key = bestKey; key != ~0ULL; key = mp[key].par) chain.push_back(key);
        std::reverse(chain.begin(), chain.end());
        for (size_t i = 0; i + 1 < chain.size(); i++) {
            int bl = -1, nbl = -1;
            for (int q = 0; q < k; q++) {
                if (((chain[i] >> (4 * q)) & 15) == 15) bl = q;
                if (((chain[i + 1] >> (4 * q)) & 15) == 15) nbl = q;
            }
            for (int d = 0; d < 4; d++) if (adj[bl][d] == nbl) { doMove(d); break; }
        }
    }
};

// ---------------------------------------------------------------------------------
// LNS по последовательности: короткие обходы между состояниями собственного пути
// ---------------------------------------------------------------------------------
class Shortcut {
public:
    Shortcut(const Ctx& x) : X(x) {}

    // один проход; true -- последовательность стала дешевле
    bool pass(Moves& mv, int D, const Clock& clk) {
        n = (int)mv.size();
        if (n < 4) return false;
        std::vector<int> a = X.a0;
        int b = (int)(std::find(a.begin(), a.end(), 0) - a.begin());
        std::vector<u64> H(n + 1);
        PC.assign(n + 1, 0);
        u64 h = 0;
        for (int i = 0; i < X.N; i++) h ^= zk(i, a[i]);
        H[0] = h;
        for (int k = 0; k < n; k++) {
            int v = X.nb(b, mv[k]), t = a[v];
            h ^= zk(b, 0) ^ zk(v, t) ^ zk(b, t) ^ zk(v, 0);
            a[b] = t; a[v] = 0; b = v;
            H[k + 1] = h;
            PC[k + 1] = PC[k] + X.bcost(mv[k]);
        }
        endHash = h;
        size_t cap = 16;
        while (cap < (size_t)(n + 1) * 2) cap <<= 1;
        tkey.assign(cap, 0); tval.assign(cap, -1);
        mask = cap - 1;
        for (int k = 0; k <= n; k++) {
            size_t s = (H[k] * 0x9E3779B97F4A7C15ULL >> 20) & mask;
            while (tval[s] >= 0) s = (s + 1) & mask;
            tkey[s] = H[k]; tval[s] = k;
        }
        // рабочее поле
        wa = X.a0;
        wb = (int)(std::find(wa.begin(), wa.end(), 0) - wa.begin());
        maxD = D;
        struct Rep { int i, j; Moves path; };
        std::vector<Rep> reps;
        int i = 0;
        while (i < n && !clk.expired()) {
            curI = i; bestSave = 0; bestJ = -1;
            limitCost = PC[n] - PC[i];
            path.clear();
            dfs(0, -1, H[i], 0, wb);
            if (bestJ > 0) {
                reps.push_back({i, bestJ, bestPath});
                for (int k = i; k < bestJ; k++) step(mv[k]);
                i = bestJ;
            } else {
                step(mv[i]);
                i++;
            }
        }
        if (reps.empty()) return false;
        Moves res;
        int pos = 0;
        for (auto& r : reps) {
            res.insert(res.end(), mv.begin() + pos, mv.begin() + r.i);
            res.insert(res.end(), r.path.begin(), r.path.end());
            pos = r.j;
        }
        res.insert(res.end(), mv.begin() + pos, mv.end());
        i64 F, cost; int P; bool ok;
        evalMoves(X, res, F, cost, P, ok);
        i64 F0, c0; int P0; bool ok0;
        evalMoves(X, mv, F0, c0, P0, ok0);
        if (!ok || P != P0 || cost >= c0) return false;  // защита от коллизий хеша
        mv.swap(res);
        return true;
    }

private:
    const Ctx& X;
    int n = 0, maxD = 6, curI = 0, bestJ = -1, wb = 0;
    i64 bestSave = 0, limitCost = 0;
    u64 endHash = 0, mask = 0;
    std::vector<i64> PC;
    std::vector<u64> tkey;
    std::vector<int> tval, wa;
    Moves path, bestPath;

    void step(int bd) {
        int v = X.nb(wb, bd), t = wa[v];
        wa[wb] = t; wa[v] = 0; wb = v;
    }
    int lookup(u64 h) const {
        size_t s = (h * 0x9E3779B97F4A7C15ULL >> 20) & mask;
        while (tval[s] >= 0) { if (tkey[s] == h) return tval[s]; s = (s + 1) & mask; }
        return -1;
    }
    void dfs(int depth, int last, u64 h, i64 c, int bl) {
        for (int bd = 0; bd < 4; bd++) {
            if (last >= 0 && bd == opp(last)) continue;
            int v = X.nb(bl, bd);
            if (v < 0) continue;
            i64 c2 = c + X.bcost(bd);
            if (c2 >= limitCost) continue;
            int t = wa[v];
            u64 h2 = h ^ zk(bl, 0) ^ zk(v, t) ^ zk(bl, t) ^ zk(v, 0);
            wa[bl] = t; wa[v] = 0;
            path.push_back((u8)bd);
            int j = lookup(h2);
            if (j > curI) {
                i64 save = PC[j] - PC[curI] - c2;
                if (save > bestSave) { bestSave = save; bestJ = j; bestPath = path; }
            }
            if (depth + 1 < maxD) dfs(depth + 1, bd, h2, c2, v);
            path.pop_back();
            wa[v] = t; wa[bl] = 0;
        }
    }
};


// ---------------------------------------------------------------------------------
// LNS-окна: между состояниями S_i и S_j пути ищем точный дешёвый путь (A* по разреженному
// состоянию = отличия от S_j). Память ограничена числом узлов.
// ---------------------------------------------------------------------------------
class WindowOpt {
public:
    explicit WindowOpt(const Ctx& x) : X(x) {
        W = x.a0;
        wpos.assign(x.N, 0);
        for (int c = 0; c < x.N; c++) wpos[W[c]] = c;
        wb = wpos[0];
    }

    bool pass(Moves& mv, int Lw, size_t cap, const Clock& clk) {
        W = X.a0;
        for (int c = 0; c < X.N; c++) wpos[W[c]] = c;
        wb = wpos[0];
        bool any = false;
        size_t i = 0;
        const size_t stride = std::max(1, Lw / 2);
        while (i + 4 <= mv.size() && !clk.expired()) {
            size_t j = std::min(mv.size(), i + (size_t)Lw);
            // клетки, затронутые окном
            cells.clear();
            {
                int b = wb;
                cells.push_back(b);
                for (size_t k = i; k < j; k++) { b = X.nb(b, mv[k]); cells.push_back(b); }
                std::sort(cells.begin(), cells.end());
                cells.erase(std::unique(cells.begin(), cells.end()), cells.end());
            }
            startC.resize(cells.size());
            for (size_t k = 0; k < cells.size(); k++) startC[k] = W[cells[k]];
            i64 seg = 0;
            for (size_t k = i; k < j; k++) { step(mv[k]); seg += X.bcost(mv[k]); }
            Moves best;
            bool found = search(seg, cap, best);
            for (size_t k = j; k > i; k--) step(opp(mv[k - 1]));  // откат к S_i
            size_t adv = stride;
            if (found) {
                mv.erase(mv.begin() + i, mv.begin() + j);
                mv.insert(mv.begin() + i, best.begin(), best.end());
                any = true;
                adv = std::min(stride, best.size());
                if (adv == 0) adv = 1;
            }
            for (size_t k = 0; k < adv && i + k < mv.size(); k++) step(mv[i + k]);
            i += adv;
        }
        return any;
    }

private:
    const Ctx& X;
    std::vector<int> W, wpos, cells, startC;
    int wb;

    void step(int bd) {
        int v = X.nb(wb, bd), t = W[v];
        W[wb] = t; wpos[t] = wb;
        W[v] = 0; wpos[0] = v;
        wb = v;
    }

    struct Node { i64 g, h; u64 hash; u32 off, len; int parent, blank; u8 mv, last; };

    inline int tileAt(const u32* src, u32 n, int q) const {
        u32 key = (u32)q << 16;
        const u32* it = std::lower_bound(src, src + n, key);
        if (it < src + n && (int)(*it >> 16) == q) return (int)(*it & 0xFFFF);
        return W[q];  // совпадает с целью
    }

    bool search(i64 bound, size_t cap, Moves& result) {
        std::vector<u32> pool;
        std::vector<Node> nodes;
        Node root{};
        u64 hash = 0;
        i64 h = 0;
        for (size_t k = 0; k < cells.size(); k++) {
            int c = cells[k], t = startC[k];
            if (t != W[c]) {
                pool.push_back(((u32)c << 16) | (u32)t);
                hash ^= zk(c, t);
                if (t) h += X.wmd(c, wpos[t]);
            }
        }
        if (pool.empty()) return false;
        root.g = 0; root.h = h; root.hash = hash; root.off = 0; root.len = (u32)pool.size();
        root.parent = -1; root.blank = -1; root.last = 4; root.mv = 0;
        // клетка пустой в начале окна: запись с тираже 0
        for (u32 e : pool) if ((e & 0xFFFF) == 0) root.blank = (int)(e >> 16);
        if (root.blank < 0) return false;  // пустая стоит на своём месте в S_i -- редко; пропускаем окно
        nodes.push_back(root);
        typedef std::pair<i64, int> QE;
        std::priority_queue<QE, std::vector<QE>, std::greater<QE>> pq;
        std::unordered_map<u64, i64> closed;
        closed.reserve(cap * 2);
        closed[hash] = 0;
        pq.push({h, 0});
        while (!pq.empty() && nodes.size() < cap) {
            QE e = pq.top(); pq.pop();
            Node nd = nodes[e.second];
            auto it = closed.find(nd.hash);
            if (it != closed.end() && it->second < nd.g) continue;
            const int p = nd.blank;
            for (int bd = 0; bd < 4; bd++) {
                if (nd.last != 4 && bd == opp(nd.last)) continue;
                int q = X.nb(p, bd);
                if (q < 0) continue;
                const u32* src = pool.data() + nd.off;
                int t = tileAt(src, nd.len, q);
                i64 g2 = nd.g + X.bcost(bd);
                int tc = wpos[t];
                i64 h2 = nd.h + X.wmd(p, tc) - X.wmd(q, tc);
                if (g2 + h2 >= bound) continue;
                bool dpb = W[p] != 0, dqb = W[q] != t, dpa = W[p] != t, dqa = W[q] != 0;
                u64 hs = nd.hash;
                if (dpb) hs ^= zk(p, 0);
                if (dqb) hs ^= zk(q, t);
                if (dpa) hs ^= zk(p, t);
                if (dqa) hs ^= zk(q, 0);
                u32 len2 = nd.len + (u32)dpa + (u32)dqa - (u32)dpb - (u32)dqb;
                auto cit = closed.find(hs);
                if (cit != closed.end() && cit->second <= g2) continue;
                closed[hs] = g2;
                if (len2 == 0) {  // достигли S_j
                    result.clear();
                    result.push_back((u8)bd);
                    for (int k = e.second; nodes[k].parent >= 0; k = nodes[k].parent) result.push_back(nodes[k].mv);
                    std::reverse(result.begin(), result.end());
                    return g2 < bound;
                }
                Node c2;
                c2.g = g2; c2.h = h2; c2.hash = hs; c2.parent = e.second; c2.blank = q; c2.mv = (u8)bd; c2.last = (u8)bd;
                c2.off = (u32)pool.size();
                const int c1 = std::min(p, q), cb = std::max(p, q);
                u32 n = nd.len, i = 0;
                std::vector<u32>& pl = pool;
                // src может переехать при росте pool -- копируем по индексу
                size_t so = nd.off;
                auto S = [&](u32 k) { return pl[so + k]; };
                std::vector<u32> tmp;
                while (i < n && (int)(S(i) >> 16) < c1) tmp.push_back(S(i++));
                if (i < n && (int)(S(i) >> 16) == c1) i++;
                { int content = (c1 == p) ? t : 0; if (content != W[c1]) tmp.push_back(((u32)c1 << 16) | (u32)content); }
                while (i < n && (int)(S(i) >> 16) < cb) tmp.push_back(S(i++));
                if (i < n && (int)(S(i) >> 16) == cb) i++;
                { int content = (cb == p) ? t : 0; if (content != W[cb]) tmp.push_back(((u32)cb << 16) | (u32)content); }
                while (i < n) tmp.push_back(S(i++));
                pool.insert(pool.end(), tmp.begin(), tmp.end());
                c2.len = (u32)tmp.size();
                nodes.push_back(c2);
                pq.push({g2 + h2, (int)nodes.size() - 1});
            }
        }
        return false;
    }
};

}  // namespace

LnsResult solve_lns(uint32_t rows, uint32_t cols, const std::array<uint32_t, 4>& weights,
                    const std::vector<uint16_t>& cells, double time_limit, int verbose) {
    Clock clk;
    clk.limit = time_limit;
    Ctx X;
    X.R = (int)rows; X.C = (int)cols; X.N = X.R * X.C;
    for (int i = 0; i < 4; i++) X.w[i] = weights[i];
    X.a0.assign(cells.begin(), cells.end());

    Moves mv;
    if (X.R == 1 || X.C == 1) mv = solveLine(X);
    else {
        struct Cf { int lam; bool sac; };
        std::vector<Cf> cfs = {{0, false}, {50, false}, {75, false}, {25, false}, {90, false}, {60, false}, {40, false}, {85, false}};
        if (X.N >= 400) cfs = {{0, true}, {60, true}, {75, true}, {0, false}, {50, false}, {75, false}, {90, true}, {40, true}};
        if (getenv("P15LAM")) cfs = {{atoi(getenv("P15LAM")), getenv("P15SAC") != nullptr}};
        i64 bestF = INT64_MIN;
        for (size_t k = 0; k < cfs.size(); k++) {
            if (k > 0 && clk.elapsed() > time_limit * 0.3) break;
            double t0 = clk.elapsed();
            Builder bld(X, clk);
            bld.lamPct = cfs[k].lam;
            bld.sacrifice = cfs[k].sac;
            if (getenv("P15THR")) bld.sacThr = atoll(getenv("P15THR"));
            Moves cand = polish(X, bld.build());
            i64 F, c; int P; bool ok;
            evalMoves(X, cand, F, c, P, ok);
            if (verbose) std::fprintf(stderr, "[build lam=%d sac=%d] F=%lld cost=%lld t=%.2fs\n", cfs[k].lam, (int)cfs[k].sac, F, c, clk.elapsed() - t0);
            if (ok && F > bestF) { bestF = F; mv = cand; }
            double dt = clk.elapsed() - t0;
            if (clk.elapsed() + dt > time_limit * 0.3) break;
        }
    }
    mv = polish(X, mv);
    auto report = [&](const char* tag) {
        if (!verbose) return;
        i64 F, c; int P; bool ok;
        evalMoves(X, mv, F, c, P, ok);
        std::fprintf(stderr, "[%s] F=%lld P=%d cost=%lld moves=%zu t=%.2fs\n", tag, F, P, c, mv.size(), clk.elapsed());
    };
    report("build");

    if (X.R > 1 && X.C > 1) {
        Shortcut sc(X);
        WindowOpt wo(X);
        int Lw = 24, D = 6;
        size_t cap = 20000;
        int idle = 0;
        while (!clk.expired() && !mv.empty() && idle < 6) {
            bool imp = wo.pass(mv, Lw, cap, clk);
            if (imp) { mv = polish(X, mv); report("win"); idle = 0; }
            else {
                idle++;
                Lw = Lw * 3 / 2 + 2;
                cap = std::min<size_t>(cap * 2, 400000);
                if (Lw > 120) { Lw = 24; cap = 20000; }
            }
            if (!clk.expired() && sc.pass(mv, D, clk)) { mv = polish(X, mv); report("hash"); idle = 0; }
        }
    }

    LnsResult res;
    i64 F, cost; int P; bool ok;
    evalMoves(X, mv, F, cost, P, ok);
    res.F = F; res.cost = cost; res.placed = P;
    {   // неразрешимость: чётность перестановки + расстояние пустой до угла
        int N = X.N;
        std::vector<char> seen(N, 0);
        int cycles = 0;
        for (int i = 0; i < N; i++) {
            if (seen[i]) continue;
            cycles++;
            for (int j = i; !seen[j];) { seen[j] = 1; int v = X.a0[j] ? X.a0[j] : N; j = v - 1; }
        }
        int b = (int)(std::find(X.a0.begin(), X.a0.end(), 0) - X.a0.begin());
        res.unsolvable = X.R > 1 && X.C > 1 && ((((N - cycles) + (X.R - 1 - b / X.C) + (X.C - 1 - b % X.C)) & 1) != 0);
    }
    std::vector<int> a = X.a0;
    int b = (int)(std::find(a.begin(), a.end(), 0) - a.begin());
    for (u8 m : mv) {
        int n = X.nb(b, m);
        res.moves.push_back({(uint16_t)a[n], DCH[opp(m)]});
        a[b] = a[n]; a[n] = 0; b = n;
    }
    return res;
}
