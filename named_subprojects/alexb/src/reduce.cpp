// reduce.cpp -- послойная сборка ("человеческий" метод, но с учётом стоимостей).
//
// Поле уменьшается: собирается верхняя строка или левый столбец текущего
// прямоугольника, затем он сужается. Каждая фишка проводится оптимальным по
// стоимости путём (Router: A* по состояниям "фишка + сторона пустой клетки").
// Две последние фишки строки/столбца ставятся точным поиском в небольшом окне
// (это покрывает и классический приём, и все "неудобные" случаи).
// Остаток <= 9 клеток доигрывается точно (exact), остаток <= 16 клеток
// дополнительно улучшается алгоритмом ida, если хватает времени.
// Пробуются разные стратегии выбора строка/столбец и направления заполнения,
// берётся лучшая; итог обрезается до лучшего по F префикса.
#include <queue>

#include "route.hpp"
#include "solvers.hpp"
#include "stages.hpp"

namespace p15 {

bool makeSubProblem(const Problem& pr, const Board& b, int r0, int c0, Problem& sub) {
    const int C = pr.C;
    sub = Problem();
    sub.R = pr.R - r0;
    sub.C = pr.C - c0;
    for (int d = 0; d < 4; d++) sub.cost[d] = pr.cost[d];
    sub.costGiven = pr.costGiven;
    sub.start.assign((size_t)sub.R * sub.C, 0);
    for (int i = 0; i < sub.R; i++)
        for (int j = 0; j < sub.C; j++) {
            int t = b.a[(r0 + i) * C + (c0 + j)];
            if (t == 0) continue;
            int tr = (t - 1) / C, tc = (t - 1) % C;
            if (tr < r0 || tc < c0) return false;   // чужая фишка
            sub.start[i * sub.C + j] = (tr - r0) * sub.C + (tc - c0) + 1;
        }
    return true;
}

// Точный поиск (Дейкстра) по состояниям (клетка ta, клетка tb, пустая клетка)
// внутри окна W: поставить ta в x1, tb в x2. Остальные фишки окна -- "безымянные".
bool windowPair(const Problem& pr, Board& b, int ta, int tb, int x1, int x2,
                const std::vector<int>& W, Moves& out) {
    const int w = (int)W.size();
    std::vector<int> idx(b.N, -1);
    for (int i = 0; i < w; i++) idx[W[i]] = i;
    int ia = idx[b.pos[ta]], ib = idx[b.pos[tb]], ie = idx[b.blank];
    int ga = idx[x1], gb = idx[x2];
    if (ia < 0 || ib < 0 || ie < 0 || ga < 0 || gb < 0) return false;
    std::vector<int> nb((size_t)w * 4, -1);
    for (int i = 0; i < w; i++)
        for (int d = 0; d < 4; d++) {
            int c = b.nb(W[i], d);
            if (c >= 0 && idx[c] >= 0) nb[i * 4 + d] = idx[c];
        }
    const size_t S = (size_t)w * w * w;
    const long long INF = (long long)4e18;
    std::vector<long long> dist(S, INF);
    std::vector<int> par(S, -1);
    std::vector<uint8_t> pm(S, 0);
    auto id = [&](int a, int bb, int e) { return ((size_t)a * w + bb) * w + e; };
    typedef std::pair<long long, size_t> QE;
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> q;
    size_t s0 = id(ia, ib, ie);
    dist[s0] = 0;
    q.push(QE(0, s0));
    size_t goal = S;
    while (!q.empty()) {
        QE e = q.top();
        q.pop();
        size_t s = e.second;
        if (e.first != dist[s]) continue;
        int a = (int)(s / ((size_t)w * w)), bb = (int)(s / w % w), em = (int)(s % w);
        if (a == ga && bb == gb) { goal = s; break; }
        for (int d = 0; d < 4; d++) {
            int n = nb[em * 4 + d];
            if (n < 0) continue;
            int na = a == n ? em : a, nbb = bb == n ? em : bb;
            size_t ns = id(na, nbb, n);
            long long nd = e.first + pr.blankCost(d);
            if (nd < dist[ns]) {
                dist[ns] = nd;
                par[ns] = (int)s;
                pm[ns] = (uint8_t)d;
                q.push(QE(nd, ns));
            }
        }
    }
    if (goal == S) return false;
    Moves mv;
    for (size_t s = goal; s != s0; s = (size_t)par[s]) mv.push_back(pm[s]);
    for (auto it = mv.rbegin(); it != mv.rend(); ++it) {
        b.move(*it);
        out.push_back(*it);
    }
    return b.a[x1] == ta && b.a[x2] == tb;
}

// Последние две клетки линии: x1 = L[k-2], x2 = L[k-1] ("карман").
// in -- направление внутрь оставшейся области, depth -- её глубина (<= 3).
bool placePair(const Problem& pr, Board& b, Router& rt, const std::vector<int>& L, int in, int depth,
               Moves& out) {
    const int k = (int)L.size();
    const int x1 = L[k - 2], x2 = L[k - 1];
    const int ta = x1 + 1, tb = x2 + 1;
    if (b.a[x1] == ta && b.a[x2] == tb) {
        rt.locked[x1] = rt.locked[x2] = 1;
        return true;
    }
    // окно: последние (до 4) клеток линии и до 3 клеток вглубь
    std::vector<int> W;
    for (int i = 0; i < std::min(k, 4); i++) {
        int c = L[k - 1 - i];
        for (int j = 0; j < depth && c >= 0; j++, c = b.nb(c, in))
            if (!rt.locked[c]) W.push_back(c);
    }
    auto inW = [&](int c) { return std::find(W.begin(), W.end(), c) != W.end(); };
    if (!inW(b.pos[ta]) || !inW(b.pos[tb]) || !inW(b.blank)) {
        // подводим ta на своё место, tb и пустую -- в окно
        if (!rt.moveTile(b, ta, x1, out)) return false;
        rt.locked[x1] = 1;
        if (b.a[x2] == tb) {
            rt.locked[x2] = 1;
            return true;
        }
        bool ok = true;
        if (!inW(b.pos[tb])) ok = rt.moveTile(b, tb, b.nb(x2, in), out);
        if (ok && !inW(b.blank)) {
            int pb = b.pos[tb];
            rt.locked[pb] = 1;
            ok = rt.moveBlankAny(b, W, -1, out);
            rt.locked[pb] = 0;
        }
        rt.locked[x1] = 0;
        if (!ok) return false;
    }
    if (!windowPair(pr, b, ta, tb, x1, x2, W, out)) return false;
    rt.locked[x1] = rt.locked[x2] = 1;
    return true;
}

bool fillLine(const Problem& pr, Board& b, Router& rt, const std::vector<int>& L, int in, int depth,
              Moves& out) {
    int k = (int)L.size();
    for (int i = 0; i + 2 < k; i++) {
        if (!rt.moveTile(b, L[i] + 1, L[i], out)) return false;
        rt.locked[L[i]] = 1;
    }
    return placePair(pr, b, rt, L, in, depth, out);
}

// линия-строка r0 (или столбец c0) текущей области в порядке заполнения
std::vector<int> lineCells(const Problem& pr, int r0, int c0, bool isRow, bool rev) {
    std::vector<int> L;
    if (isRow)
        for (int c = c0; c < pr.C; c++) L.push_back(r0 * pr.C + c);
    else
        for (int r = r0; r < pr.R; r++) L.push_back(r * pr.C + c0);
    if (rev) std::reverse(L.begin(), L.end());
    return L;
}

bool applyMoves(Board& b, const Moves& mv, Moves& out) {
    for (uint8_t m : mv) {
        if (!b.canMove(m)) return false;
        b.move(m);
        out.push_back(m);
    }
    return true;
}

namespace {

struct RunResult {
    Moves moves;
    bool complete = false;
    bool haveCk = false;   // контрольная точка: остаток <= 16 клеток
    size_t ckLen = 0;
    int ckR0 = 0, ckC0 = 0;
};

// Один прогон послойной сборки.
// mode: 0 -- длинная сторона (ничья -> строка), 1 -- длинная сторона (ничья -> столбец),
//       2 -- пробный выбор (4 варианта, минимум стоимости на фишку), >=3 -- случайный выбор.
RunResult runReduce(const Problem& pr, const Options& opt, const Timer& tm, Rng& rng, int mode,
                    long long stopBelow, int exactCells) {
    RunResult res;
    const int R = pr.R, C = pr.C, K = R * C - 1;
    Board b(pr);
    Router rt(pr);
    int r0 = 0, c0 = 0;
    long long cost = 0;
    Moves& out = res.moves;
    while (true) {
        int h = R - r0, w = C - c0;
        if (!res.haveCk && h * w <= 16) {
            res.haveCk = true;
            res.ckLen = out.size();
            res.ckR0 = r0;
            res.ckC0 = c0;
        }
        if (h * w <= exactCells) {
            Problem sub;
            if (!makeSubProblem(pr, b, r0, c0, sub)) return res;
            Options o2 = opt;
            o2.verbose = 0;
            Moves em = solveExact(sub, o2, Timer(0));
            if (!applyMoves(b, em, out)) return res;
            res.complete = true;
            return res;
        }
        if (tm.expired()) return res;
        // выбор этапа
        bool isRow;
        bool rev = false;
        if (h == 2) isRow = false;
        else if (w == 2) isRow = true;
        else if (mode == 0) isRow = h >= w;
        else if (mode == 1) isRow = h > w;
        else if (mode == 2) {
            // пробуем 4 варианта и берём лучший по стоимости на фишку
            double bestRate = 1e300;
            int bestOpt = -1;
            for (int o = 0; o < 4; o++) {
                bool ir = o < 2, rv = (o & 1) != 0;
                Board b2 = b;
                std::vector<uint8_t> lk = rt.locked;
                Moves tmp;
                std::vector<int> L = lineCells(pr, r0, c0, ir, rv);
                bool ok = fillLine(pr, b2, rt, L, ir ? DOWN : RIGHT, std::min(3, ir ? h : w), tmp);
                rt.locked = lk;
                if (!ok) continue;
                long long cst = 0;
                for (uint8_t m : tmp) cst += pr.blankCost(m);
                double rate = (double)(cst + 1) / (double)L.size();
                if (rate < bestRate) { bestRate = rate; bestOpt = o; }
            }
            if (bestOpt < 0) return res;
            isRow = bestOpt < 2;
            rev = (bestOpt & 1) != 0;
        } else {
            isRow = rng.below(h + w - 4) < h - 2;
            rev = rng.below(2) == 1;
        }
        if (mode >= 3 && (h == 2 || w == 2)) rev = rng.below(2) == 1;
        std::vector<int> L = lineCells(pr, r0, c0, isRow, rev);
        size_t before = out.size();
        if (!fillLine(pr, b, rt, L, isRow ? DOWN : RIGHT, std::min(3, isRow ? h : w), out)) return res;
        for (size_t i = before; i < out.size(); i++) cost += pr.blankCost(out[i]);
        if (isRow) r0++;
        else c0++;
        // если даже идеальное продолжение не догонит рекорд -- прекращаем
        long long Fnow = TILE_VALUE * b.placed() - cost;
        long long upper = Fnow + TILE_VALUE * (K - b.placed());
        if (upper <= stopBelow) return res;
        (void)opt;
    }
}

}  // namespace

Moves solveReduce(const Problem& pr, const Options& opt, const Timer& tm) {
    Rng rng(opt.seed);
    const int exactCells = (int)opt.getI("exact-cells", 9);
    const int maxRuns = (int)opt.getI("runs", tm.limit > 0 ? 1000000 : 20);
    const bool useIda = opt.getI("ida-endgame", 1) != 0;

    Moves best;
    long long bestF = scoreOfEmpty(pr).F;
    // внешний рекорд (от портфеля): прогоны, которые его заведомо не превзойдут, обрываются
    const long long stopBelow = std::max(bestF, (long long)opt.getI("stop-below", bestF));
    RunResult bestRun;
    long long bestRunF = 0;
    bool haveRun = false;
    int runs = 0, lastImprove = 0;
    for (int mode = 0; runs < maxRuns; mode++, runs++) {
        if (runs > 0 && tm.expired()) break;
        if (runs - lastImprove > 400) break;   // давно нет улучшений
        // случайные прогоны -- только в первой половине отведённого времени
        if (mode >= 2 && tm.limit > 0 && tm.elapsed() > 0.5 * tm.limit) break;
        RunResult rr = runReduce(pr, opt, tm, rng, mode, std::max(bestF, stopBelow), exactCells);
        Moves pm = polish(pr, rr.moves);
        Score sc;
        simulate(pr, pm, sc);
        logf(2, opt, "[reduce] run %d mode %d: complete=%d moves=%zu F=%lld\n", runs, mode,
             (int)rr.complete, pm.size(), sc.F);
        if (sc.F > bestF) {
            bestF = sc.F;
            best = pm;
            lastImprove = runs;
        }
        if (rr.complete && (!haveRun || sc.F > bestRunF)) {
            bestRun = rr;
            bestRunF = sc.F;
            haveRun = true;
        }
        if (pr.cells() > 2500 && mode >= 1) break;   // большие поля: два прогона
    }
    logf(1, opt, "[reduce] %d runs, best F = %lld\n", runs, bestF);

    // доигрывание последнего фрагмента (<= 16 клеток) алгоритмом ida
    // (только если поле действительно было уменьшено: иначе это просто алгоритм ida)
    if (useIda && haveRun && bestRun.haveCk && bestRun.ckR0 + bestRun.ckC0 > 0 && !tm.expired()) {
        Board b(pr);
        Moves prefix(bestRun.moves.begin(), bestRun.moves.begin() + (long)bestRun.ckLen);
        for (uint8_t m : prefix) b.move(m);
        Problem sub;
        if (makeSubProblem(pr, b, bestRun.ckR0, bestRun.ckC0, sub)) {
            Moves tail(bestRun.moves.begin() + (long)bestRun.ckLen, bestRun.moves.end());
            double tl = tm.limit > 0 ? std::max(0.5, tm.remaining()) : 30.0;
            Options o2 = opt;
            o2.verbose = std::max(0, opt.verbose - 1);
            IdaResult ir = idaSearch(sub, tail, tl, o2);
            Moves cand = prefix;
            cand.insert(cand.end(), ir.moves.begin(), ir.moves.end());
            Moves pm = polish(pr, cand);
            Score sc;
            if (simulate(pr, pm, sc) && sc.F > bestF) {
                logf(1, opt, "[reduce] ida endgame: F %lld -> %lld%s\n", bestF, sc.F,
                     ir.optimal ? " (endgame optimal)" : "");
                bestF = sc.F;
                best = pm;
            }
        }
    }
    return best;
}

}  // namespace p15
