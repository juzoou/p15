// ida.cpp -- точный поиск IDA* в форме "ветвей и границ" по целевой функции F.
//
// Ищется максимум F = 100*P(s) - g(s) по всем позициям s, а не только путь до
// собранного поля. Верхняя граница для узла: UB = -g + U(s), где
//   U_full(s) = 100*Kgoal - h(s)    -- для целевой позиции (h: взвешенное
//               манхэттенское расстояние + линейные конфликты, или аддитивные БД
//               образцов, что больше);
//   U_part(s) = sum_t max(0, 100 - md_t(s)) - (m наименьших слагаемых)
//               -- для позиций, где не на месте хотя бы m фишек.
// Для неразрешимых задач перебираются цели G_X: все фишки на местах, кроме X,
// стоящей в углу (пустая -- на месте X); первая из этих задач дополнительно
// покрывает все позиции с >= 2 фишками не на месте. Если все поиски завершены,
// найденное решение оптимально.
// Порог итераций IDA* понижается адаптивно; есть таблица транспозиций.
#include "pdb.hpp"
#include "solvers.hpp"

namespace p15 {

namespace {

const long long NEG = -(1LL << 60);

struct TTE {
    uint64_t key;
    uint64_t val;   // (номер итерации << 44) | g
};

class IdaCore {
public:
    IdaCore(const Problem& pr, int X, int missing, const std::vector<const PatternDB*>& dbs, int ttBits)
        : R(pr.R), C(pr.C), N(pr.R * pr.C), K(pr.R * pr.C - 1), missing(missing), ttBits(ttBits) {
        for (int d = 0; d < 4; d++) {
            cst[d] = pr.cost[d];
            bc[d] = pr.cost[d ^ 1];
        }
        nbT.assign((size_t)N * 4, -1);
        rowOf.resize(N);
        colOf.resize(N);
        for (int c = 0; c < N; c++) {
            rowOf[c] = c / C;
            colOf[c] = c % C;
            for (int d = 0; d < 4; d++) {
                int r2 = c / C + DROW[d], c2 = c % C + DCOL[d];
                if (r2 >= 0 && r2 < R && c2 >= 0 && c2 < C) nbT[(size_t)c * 4 + d] = r2 * C + c2;
            }
        }
        tT.assign(N, 0);
        for (int t = 1; t <= K; t++) tT[t] = t - 1;
        tG = tT;
        if (X > 0) tG[X] = N - 1;
        Kgoal = X > 0 ? K - 1 : K;
        a = pr.start;
        pos.assign(N, 0);
        for (int c = 0; c < N; c++) pos[a[c]] = c;
        blank = pos[0];
        buf.resize(std::max(R, C) + 2);
        tails.resize(std::max(R, C) + 2);
        // начальные значения
        P = 0;
        sumM = 0;
        sumMdG = 0;
        for (int v = 0; v <= 100; v++) hist[v] = 0;
        mdT.assign(N, 0);
        mdG.assign(N, 0);
        mval.assign(N, 0);
        hash = 0;
        for (int c = 0; c < N; c++) hash ^= zkey(c, a[c]);
        for (int t = 1; t <= K; t++) {
            int p = pos[t];
            if (p == tT[t]) P++;
            mdT[t] = wmd(p, tT[t]);
            mval[t] = (int)std::max(0LL, TILE_VALUE - mdT[t]);
            hist[mval[t]]++;
            sumM += mval[t];
            mdG[t] = wmd(p, tG[t]);
            sumMdG += mdG[t];
        }
        minV = 0;
        lcRow.assign(R, 0);
        lcCol.assign(C, 0);
        lcSum = 0;
        for (int r = 0; r < R; r++) lcSum += lcRow[r] = rowLC(r);
        for (int c = 0; c < C; c++) lcSum += lcCol[c] = colLC(c);
        // БД образцов
        patOf.assign(N, -1);
        slotOf.assign(N, -1);
        pdbSum = 0;
        for (size_t i = 0; i < dbs.size(); i++) {
            pdbs.push_back(dbs[i]);
            std::vector<int> cells;
            for (size_t j = 0; j < dbs[i]->tiles.size(); j++) {
                int t = dbs[i]->tiles[j];
                patOf[t] = (int)i;
                slotOf[t] = (int)j;
                cells.push_back(pos[t]);
            }
            patCells.push_back(cells);
            long long v = dbs[i]->value(dbs[i]->index(patCells.back().data()));
            pdbVal.push_back(v);
            pdbSum += v;
        }
    }

    long long bound() const {
        long long u = NEG;
        long long hf = sumMdG + lcSum;
        if (pdbSum > hf) hf = pdbSum;
        u = TILE_VALUE * Kgoal - hf;
        if (missing > 0) {
            long long up = sumM - smallestM(missing);
            if (up > u) u = up;
        }
        return u;
    }
    long long fullBound() const {
        long long hf = sumMdG + lcSum;
        if (pdbSum > hf) hf = pdbSum;
        return TILE_VALUE * Kgoal - hf;
    }

    // true -- итерации завершены (для своей цели найден оптимум)
    bool run(const Timer& timer, long long& bestFio, Moves& bestPathIo, uint64_t& nodeCount, const Options& opt) {
        tm = &timer;
        bestF = bestFio;
        improved = false;
        tt.assign((size_t)1 << ttBits, TTE{0, 0});
        ttMask = ((uint64_t)1 << ttBits) - 1;
        T = bound();
        long long delta = 1;
        uint64_t prevNodes = 0;
        bool proven = T <= bestF;
        while (!proven) {
            iter++;
            nextT = NEG;
            aborted = false;
            nodes = 0;
            ttVisit(0);
            path.clear();
            dfs(0, -1);
            nodeCount += nodes;
            logf(3, opt, "[ida]   threshold %lld: nodes %llu, next %lld, best %lld\n", T,
                 (unsigned long long)nodes, nextT, bestF);
            if (aborted) break;
            if (nextT <= bestF) { proven = true; break; }
            if (prevNodes > 0 && nodes < 3 * prevNodes) delta = std::min(delta * 2, (1LL << 40));
            else if (nodes > 30 * prevNodes && delta > 1) delta /= 2;
            long long nt = std::min(nextT, T - delta);
            nt = std::max(nt, bestF + 1);
            prevNodes = nodes;
            T = nt;
        }
        if (improved) {
            bestFio = bestF;
            bestPathIo = bestPath;
        }
        tt.clear();
        tt.shrink_to_fit();
        return proven;
    }

private:
    int R, C, N, K;
    int missing;
    int ttBits;
    long long cst[4], bc[4];
    std::vector<int> nbT, rowOf, colOf, tT, tG;
    int Kgoal = 0;
    std::vector<int> a, pos;
    int blank = 0, P = 0;
    std::vector<long long> mdT, mdG;
    std::vector<int> mval;
    int hist[101];
    mutable int minV = 0;
    long long sumM = 0, sumMdG = 0;
    std::vector<long long> lcRow, lcCol;
    long long lcSum = 0;
    std::vector<int> buf, tails;
    std::vector<const PatternDB*> pdbs;
    std::vector<int> patOf, slotOf;
    std::vector<std::vector<int>> patCells;
    std::vector<long long> pdbVal;
    long long pdbSum = 0;
    uint64_t hash = 0;
    // поиск
    const Timer* tm = nullptr;
    long long T = 0, nextT = 0, bestF = 0;
    bool improved = false, aborted = false;
    uint64_t nodes = 0, iter = 0;
    Moves path, bestPath;
    std::vector<TTE> tt;
    uint64_t ttMask = 0;

    long long wmd(int cell, int target) const {
        int dr = rowOf[target] - rowOf[cell], dc = colOf[target] - colOf[cell];
        long long h = dr > 0 ? dr * cst[DOWN] : -dr * cst[UP];
        h += dc > 0 ? dc * cst[RIGHT] : -dc * cst[LEFT];
        return h;
    }
    long long smallestM(int k) const {
        long long s = 0;
        int need = k;
        int v = minV;
        while (v <= 100 && hist[v] == 0) v++;
        minV = v;
        for (; v <= 100 && need > 0; v++) {
            int c = std::min(hist[v], need);
            s += (long long)c * v;
            need -= c;
        }
        return s;
    }
    int lis(int n) {
        int len = 0;
        for (int i = 0; i < n; i++) {
            int x = buf[i];
            int lo = 0, hi = len;
            while (lo < hi) {
                int mid = (lo + hi) >> 1;
                if (tails[mid] < x) lo = mid + 1;
                else hi = mid;
            }
            tails[lo] = x;
            if (lo == len) len++;
        }
        return len;
    }
    long long rowLC(int r) {
        int n = 0;
        for (int c = 0; c < C; c++) {
            int t = a[r * C + c];
            if (t && rowOf[tG[t]] == r) buf[n++] = colOf[tG[t]];
        }
        if (n < 2) return 0;
        return (long long)(n - lis(n)) * (cst[UP] + cst[DOWN]);
    }
    long long colLC(int c) {
        int n = 0;
        for (int r = 0; r < R; r++) {
            int t = a[r * C + c];
            if (t && colOf[tG[t]] == c) buf[n++] = rowOf[tG[t]];
        }
        if (n < 2) return 0;
        return (long long)(n - lis(n)) * (cst[LEFT] + cst[RIGHT]);
    }

    void doMove(int bd) {
        int n = nbT[(size_t)blank * 4 + bd];
        int t = a[n];
        int b0 = blank;
        if (n == tT[t]) P--;
        if (b0 == tT[t]) P++;
        long long nm = wmd(b0, tT[t]);
        int mv = (int)std::max(0LL, TILE_VALUE - nm);
        hist[mval[t]]--;
        hist[mv]++;
        if (mv < minV) minV = mv;
        sumM += mv - mval[t];
        mval[t] = mv;
        mdT[t] = nm;
        long long ng = wmd(b0, tG[t]);
        sumMdG += ng - mdG[t];
        mdG[t] = ng;
        hash ^= zkey(n, t) ^ zkey(b0, t) ^ zkey(n, 0) ^ zkey(b0, 0);
        a[b0] = t;
        pos[t] = b0;
        a[n] = 0;
        pos[0] = n;
        blank = n;
        if (bd == UP || bd == DOWN) {
            int gr = rowOf[tG[t]];
            if (gr == rowOf[n] || gr == rowOf[b0]) {
                long long v = rowLC(gr);
                lcSum += v - lcRow[gr];
                lcRow[gr] = v;
            }
        } else {
            int gc = colOf[tG[t]];
            if (gc == colOf[n] || gc == colOf[b0]) {
                long long v = colLC(gc);
                lcSum += v - lcCol[gc];
                lcCol[gc] = v;
            }
        }
        int pi = patOf[t];
        if (pi >= 0) {
            patCells[pi][slotOf[t]] = b0;
            long long v = pdbs[pi]->value(pdbs[pi]->index(patCells[pi].data()));
            pdbSum += v - pdbVal[pi];
            pdbVal[pi] = v;
        }
    }

    bool ttVisit(long long g) {
        if (g >= (1LL << 44)) return true;
        TTE& e = tt[hash & ttMask];
        uint64_t it = iter & 0xFFFFF;
        if (e.key == hash && (e.val >> 44) == it) {
            long long gs = (long long)(e.val & ((1ULL << 44) - 1));
            if (gs <= g) return false;
        }
        e.key = hash;
        e.val = (it << 44) | (uint64_t)g;
        return true;
    }

    void dfs(long long g, int prev) {
        if ((++nodes & 1023) == 0 && tm->expired()) {
            aborted = true;
            return;
        }
        long long F = TILE_VALUE * P - g;
        if (F > bestF) {
            bestF = F;
            bestPath = path;
            improved = true;
        }
        for (int bd = 0; bd < 4; bd++) {
            if (bd == (prev ^ 1) || nbT[(size_t)blank * 4 + bd] < 0) continue;
            doMove(bd);
            long long g2 = g + bc[bd];
            long long ub = bound() - g2;
            if (ub < T) {
                if (ub > nextT) nextT = ub;
            } else if (ub > bestF && ttVisit(g2)) {
                path.push_back((uint8_t)bd);
                dfs(g2, bd);
                path.pop_back();
            }
            doMove(bd ^ 1);
            if (aborted) return;
        }
    }
};

struct PdbSet {
    std::vector<std::vector<int>> groups;
    std::vector<PatternDB> dbs;
    bool ok = false;
};

// БД образцов для цели: фишка t -> клетка t-1, кроме X (если X > 0) -> угол
bool buildPdbs(const Problem& pr, int k, size_t maxStates, int X, PdbSet& ps, const PdbSet* base) {
    const int N = pr.cells();
    if (!base) ps.groups = makePartition(pr.R, pr.C, k);
    else ps.groups = base->groups;
    ps.dbs.resize(ps.groups.size());
    for (size_t i = 0; i < ps.groups.size(); i++) {
        bool hasX = std::find(ps.groups[i].begin(), ps.groups[i].end(), X) != ps.groups[i].end();
        if (base && !hasX) {
            ps.dbs[i] = base->dbs[i];
            continue;
        }
        std::vector<int> goal;
        for (int t : ps.groups[i]) goal.push_back(t == X ? N - 1 : t - 1);
        if (!ps.dbs[i].build(pr.R, pr.C, ps.groups[i], goal, pr.cost, maxStates)) return ps.ok = false;
    }
    return ps.ok = true;
}

}  // namespace

IdaResult idaSearch(const Problem& pr, const Moves& incumbent, double timeLimit, const Options& opt) {
    IdaResult res;
    Timer tm(timeLimit);
    Score sc;
    Moves inc = incumbent;
    if (!simulate(pr, inc, sc)) {
        inc.clear();
        simulate(pr, inc, sc);
    }
    long long bestF = sc.F;
    Moves bestPath = inc;
    res.moves = inc;
    res.F = bestF;
    if (pr.R < 2 || pr.C < 2 || pr.allCostsZero()) return res;

    const int N = pr.cells();
    const int ttBits = (int)opt.getI("tt-bits", 21);
    int kpat = (int)opt.getI("pdb-k", N <= 16 ? 5 : (N <= 25 ? 4 : 0));
    size_t maxStates = (size_t)opt.getI("pdb-max-states", 12000000);
    PdbSet base;
    if (kpat > 0) buildPdbs(pr, kpat, maxStates, -1, base, nullptr);
    auto ptrs = [](const PdbSet& ps) {
        std::vector<const PatternDB*> v;
        if (ps.ok)
            for (auto& d : ps.dbs) v.push_back(&d);
        return v;
    };
    logf(1, opt, "[ida] pdb: %s (k=%d), %.2fs\n", base.ok ? "yes" : "no", kpat, tm.elapsed());

    uint64_t nodes = 0;
    bool allProven = true;
    if (goalReachable(pr)) {
        IdaCore core(pr, -1, 1, ptrs(base), ttBits);
        allProven = core.run(tm, bestF, bestPath, nodes, opt);
    } else {
        // кандидаты X: целевая клетка на чётном расстоянии от угла
        const int R = pr.R, C = pr.C, K = N - 1;
        std::vector<std::pair<long long, int>> cand;
        for (int X = 1; X <= K; X++) {
            int r = (X - 1) / C, c = (X - 1) % C;
            if (((R - 1 - r) + (C - 1 - c)) % 2 != 0) continue;
            IdaCore probe(pr, X, 0, {}, 1);
            cand.push_back({-probe.fullBound(), X});
        }
        std::sort(cand.begin(), cand.end());
        bool first = true;
        for (auto& cx : cand) {
            if (tm.expired()) { allProven = false; break; }
            long long rootBound = -cx.first;
            if (!first && rootBound <= bestF) continue;   // эта цель заведомо не лучше
            int X = cx.second;
            PdbSet px;
            if (base.ok) buildPdbs(pr, kpat, maxStates, X, px, &base);
            IdaCore core(pr, X, first ? 2 : 0, ptrs(px), ttBits);
            if (!first && core.bound() <= bestF) continue;
            bool pv = core.run(tm, bestF, bestPath, nodes, opt);
            logf(2, opt, "[ida] goal X=%d: %s, best F = %lld\n", X, pv ? "done" : "aborted", bestF);
            if (!pv) allProven = false;
            first = false;
        }
    }
    logf(1, opt, "[ida] nodes %llu, best F = %lld, %s, %.2fs\n", (unsigned long long)nodes, bestF,
         allProven ? "OPTIMAL" : "not proven", tm.elapsed());
    res.moves = bestPath;
    res.F = bestF;
    res.optimal = allProven;
    return res;
}

Moves solveIda(const Problem& pr, const Options& opt, const Timer& tm) {
    if (pr.allCostsZero()) return solveReduce(pr, opt, tm);   // любой полный сбор оптимален
    Moves inc;
    if (opt.getI("warm", 1) && pr.cells() <= 400) {
        Options o2 = opt;
        o2.params["runs"] = "2";
        o2.params["ida-endgame"] = "0";
        o2.verbose = 0;
        double lim = tm.limit > 0 ? std::max(0.5, tm.remaining() * 0.1) : 10.0;
        inc = solveReduce(pr, o2, Timer(lim));
    }
    IdaResult r = idaSearch(pr, inc, tm.limit > 0 ? std::max(0.1, tm.remaining()) : 0, opt);
    return r.moves;
}

}  // namespace p15
