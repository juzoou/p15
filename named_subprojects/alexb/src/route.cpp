// route.cpp -- оптимальная (по стоимости) проводка одной фишки.
//
// Состояние поиска: (клетка фишки p, сторона s, с которой к ней примыкает пустая).
// Переходы: (1) фишка сдвигается в пустую клетку -- стоимость хода фишки;
//           (2) пустая обходит фишку со стороны s на сторону s2 -- стоимость
//               кратчайшего пути пустой в обход фишки и запретных клеток.
// Начальные состояния -- путь пустой клетки до одной из сторон фишки.
// Поиск -- A* с допустимой эвристикой (взвешенное манхэттенское расстояние +
// минимум два хода пустой между соседними ходами фишки).
#include "route.hpp"

#include <algorithm>
#include <queue>

namespace p15 {

static const long long INF = (long long)4e18;

Router::Router(const Problem& p) : pr(p), R(p.R), C(p.C), N(p.R * p.C) {
    locked.assign(N, 0);
    gd.assign(N, INF);
    gpar.assign(N, -1);
    aroundGen.assign((size_t)N * 16, 0);
    aroundVal.assign((size_t)N * 16, 0);
    sd.assign((size_t)N * 4, INF);
    spar.assign((size_t)N * 4, -1);
    cmin = std::min(std::min(p.cost[0], p.cost[1]), std::min(p.cost[2], p.cost[3]));
}

long long Router::hTile(int p, int goal) const {
    int r = p / C, c = p % C, gr = goal / C, gc = goal % C;
    long long h = 0;
    int md = 0;
    if (gr > r) { h += (long long)(gr - r) * pr.cost[DOWN]; md += gr - r; }
    else        { h += (long long)(r - gr) * pr.cost[UP];   md += r - gr; }
    if (gc > c) { h += (long long)(gc - c) * pr.cost[RIGHT]; md += gc - c; }
    else        { h += (long long)(c - gc) * pr.cost[LEFT];  md += c - gc; }
    if (md > 1) h += 2 * cmin * (md - 1);
    return h;
}

int Router::blankDijkstra(int src, const int* tg, int nt, int avoid, long long* outCost) {
    for (int c : gtouched) { gd[c] = INF; gpar[c] = -1; }
    gtouched.clear();
    int remaining = 0, found = 0;
    for (int k = 0; k < nt; k++) {
        outCost[k] = -1;
        if (tg[k] >= 0) remaining++;
    }
    typedef std::pair<long long, int> QE;
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> q;
    gd[src] = 0;
    gtouched.push_back(src);
    q.push(QE(0, src));
    while (!q.empty() && remaining > 0) {
        QE e = q.top();
        q.pop();
        long long d = e.first;
        int u = e.second;
        if (d != gd[u]) continue;
        for (int k = 0; k < nt; k++)
            if (tg[k] == u && outCost[k] < 0) { outCost[k] = d; remaining--; found++; }
        for (int dir = 0; dir < 4; dir++) {
            int v = nbr(u, dir);
            if (v < 0 || v == avoid || locked[v]) continue;
            long long nd = d + pr.blankCost(dir);
            if (nd < gd[v]) {
                if (gd[v] == INF) gtouched.push_back(v);
                gd[v] = nd;
                gpar[v] = u;
                q.push(QE(nd, v));
            }
        }
    }
    return found;
}

void Router::extractBlankPath(int src, int dst, Moves& path) const {
    Moves tmp;
    for (int c = dst; c != src;) {
        int p = gpar[c];
        tmp.push_back((uint8_t)dirBetween(C, p, c));
        c = p;
    }
    path.insert(path.end(), tmp.rbegin(), tmp.rend());
}

long long Router::aroundCost(int p, int s1, int s2, Moves* path) {
    size_t key = (size_t)p * 16 + (size_t)s1 * 4 + (size_t)s2;
    if (!path && aroundGen[key] == gen) return aroundVal[key];
    int a = nbr(p, s1), z = nbr(p, s2);
    long long res = -1;
    // сначала -- Дейкстра в окне 7x7 вокруг фишки
    const int W = 3;
    int pr_ = p / C, pc = p % C;
    int r1 = std::max(0, pr_ - W), r2 = std::min(R - 1, pr_ + W);
    int c1 = std::max(0, pc - W), c2 = std::min(C - 1, pc + W);
    int ww = c2 - c1 + 1, wh = r2 - r1 + 1, n = ww * wh;
    long long dist[49];
    int par[49];
    bool done[49];
    for (int i = 0; i < n; i++) { dist[i] = INF; par[i] = -1; done[i] = false; }
    auto loc = [&](int cell) { return (cell / C - r1) * ww + (cell % C - c1); };
    auto glob = [&](int li) { return (r1 + li / ww) * C + (c1 + li % ww); };
    int src = loc(a), target = loc(z);
    dist[src] = 0;
    while (true) {
        int u = -1;
        long long best = INF;
        for (int i = 0; i < n; i++)
            if (!done[i] && dist[i] < best) { best = dist[i]; u = i; }
        if (u < 0 || u == target) break;
        done[u] = true;
        int gu = glob(u);
        for (int dir = 0; dir < 4; dir++) {
            int gv = nbr(gu, dir);
            if (gv < 0 || gv == p || locked[gv]) continue;
            int vr = gv / C, vc = gv % C;
            if (vr < r1 || vr > r2 || vc < c1 || vc > c2) continue;
            int v = loc(gv);
            long long nd = dist[u] + pr.blankCost(dir);
            if (nd < dist[v]) { dist[v] = nd; par[v] = u; }
        }
    }
    if (dist[target] < INF) {
        res = dist[target];
        if (path) {
            Moves tmp;
            for (int c = target; c != src;) {
                int pp = par[c];
                tmp.push_back((uint8_t)dirBetween(C, glob(pp), glob(c)));
                c = pp;
            }
            path->insert(path->end(), tmp.rbegin(), tmp.rend());
        }
    } else {   // в окне пути нет -- ищем по всему полю
        long long cst;
        if (blankDijkstra(a, &z, 1, p, &cst) == 1) {
            res = cst;
            if (path) extractBlankPath(a, z, *path);
        }
    }
    aroundGen[key] = gen;
    aroundVal[key] = res;
    return res;
}

long long Router::search(const Board& b, int t, int goal, long long limit, std::vector<int>* chain) {
    if (++gen == 0) {
        std::fill(aroundGen.begin(), aroundGen.end(), 0);
        gen = 1;
    }
    int p0 = b.pos[t];
    if (chain) chain->clear();
    if (p0 == goal) return 0;
    if (locked[goal] || locked[p0]) return -1;
    int tg[4];
    long long sc[4];
    for (int s = 0; s < 4; s++) {
        int c = nbr(p0, s);
        tg[s] = (c >= 0 && !locked[c]) ? c : -1;
    }
    blankDijkstra(b.blank, tg, 4, p0, sc);
    for (int st : stouched) { sd[st] = INF; spar[st] = -1; }
    stouched.clear();
    typedef std::pair<long long, int> QE;
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> q;
    long long h0 = hTile(p0, goal);
    for (int s = 0; s < 4; s++) {
        if (tg[s] < 0 || sc[s] < 0) continue;
        int st = p0 * 4 + s;
        sd[st] = sc[s];
        spar[st] = -1;
        stouched.push_back(st);
        q.push(QE(sc[s] + h0, st));
    }
    while (!q.empty()) {
        QE e = q.top();
        q.pop();
        int st = e.second, p = st >> 2, s = st & 3;
        long long g = sd[st];
        if (e.first != g + hTile(p, goal)) continue;   // устаревшая запись
        if (limit >= 0 && e.first > limit) return -1;
        if (p == goal) {
            if (chain) {
                for (int x = st; x >= 0; x = spar[x]) chain->push_back(x);
                std::reverse(chain->begin(), chain->end());
            }
            return g;
        }
        auto relax = [&](int ns, long long ng) {
            if (ng < sd[ns]) {
                if (sd[ns] == INF) stouched.push_back(ns);
                sd[ns] = ng;
                spar[ns] = st;
                q.push(QE(ng + hTile(ns >> 2, goal), ns));
            }
        };
        int qc = nbr(p, s);   // здесь пустая клетка
        relax(qc * 4 + opposite(s), g + pr.cost[s]);
        for (int s2 = 0; s2 < 4; s2++) {
            if (s2 == s) continue;
            int w = nbr(p, s2);
            if (w < 0 || locked[w]) continue;
            long long c = aroundCost(p, s, s2, nullptr);
            if (c >= 0) relax(p * 4 + s2, g + c);
        }
    }
    return -1;
}

long long Router::tileCost(const Board& b, int t, int goal, long long limit) {
    return search(b, t, goal, limit, nullptr);
}

bool Router::moveTile(Board& b, int t, int goal, Moves& out) {
    std::vector<int> chain;
    long long cost = search(b, t, goal, -1, &chain);
    if (cost < 0) return false;
    lastCost = cost;
    if (chain.empty()) return true;
    Moves mv;
    int p0 = b.pos[t];
    int side = nbr(p0, chain[0] & 3);
    long long dummy;
    blankDijkstra(b.blank, &side, 1, p0, &dummy);
    extractBlankPath(b.blank, side, mv);
    for (size_t i = 1; i < chain.size(); i++) {
        int a = chain[i - 1], z = chain[i];
        int pa = a >> 2, sa = a & 3, pz = z >> 2, sz = z & 3;
        if (pa != pz) mv.push_back((uint8_t)opposite(sa));
        else aroundCost(pa, sa, sz, &mv);
    }
    for (uint8_t m : mv) {
        int src = b.nb(b.blank, m);
        if (src < 0 || locked[src]) return false;   // не должно случаться
        b.move(m);
        out.push_back(m);
    }
    return b.pos[t] == goal;
}

bool Router::moveBlankAny(Board& b, const std::vector<int>& goals, int avoid, Moves& out) {
    std::vector<int> tg;
    for (int g : goals)
        if (g >= 0 && g != avoid && !locked[g]) tg.push_back(g);
    if (tg.empty()) return false;
    for (int g : tg)
        if (g == b.blank) { lastCost = 0; return true; }
    std::vector<long long> cst(tg.size());
    blankDijkstra(b.blank, tg.data(), (int)tg.size(), avoid, cst.data());
    int best = -1;
    for (size_t i = 0; i < tg.size(); i++)
        if (cst[i] >= 0 && (best < 0 || cst[i] < cst[best])) best = (int)i;
    if (best < 0) return false;
    Moves mv;
    extractBlankPath(b.blank, tg[best], mv);
    for (uint8_t m : mv) {
        b.move(m);
        out.push_back(m);
    }
    lastCost = cst[best];
    return true;
}

bool Router::moveBlank(Board& b, int goal, int avoid, Moves& out) {
    if (b.blank == goal) { lastCost = 0; return true; }
    if (goal < 0 || locked[goal] || goal == avoid) return false;
    long long c;
    if (blankDijkstra(b.blank, &goal, 1, avoid, &c) != 1) return false;
    Moves mv;
    extractBlankPath(b.blank, goal, mv);
    for (uint8_t m : mv) {
        b.move(m);
        out.push_back(m);
    }
    lastCost = c;
    return true;
}

}  // namespace p15
