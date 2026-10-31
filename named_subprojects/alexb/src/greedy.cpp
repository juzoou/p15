// greedy.cpp -- частичная сборка для больших полей.
//
// На больших полях полная сборка стоит больше, чем приносит (каждая фишка даёт
// 100, а её перемещение -- десятки и сотни ходов). Алгоритм ставит на места
// только выгодные фишки: на каждом шаге оценивает кандидатов (быстрая оценка ->
// точная стоимость проводки Router'ом), выбирает максимальную прибыль
// 100 - стоимость - штраф (+ поправка на то, как маршрут сдвигает остальные
// фишки), проводит фишку и фиксирует её. Фишки, уже стоящие на местах,
// фиксируются сразу. Фиксация клетки не должна разрывать свободную область
// (иначе часть поля станет недоступной для пустой клетки). Если есть время,
// делается несколько прогонов с разными параметрами.
#include <queue>

#include "route.hpp"
#include "solvers.hpp"

namespace p15 {

namespace {

class Greedy {
public:
    Greedy(const Problem& pr, const Options& opt, const Timer& tm)
        : pr(pr), opt(opt), tm(tm), R(pr.R), C(pr.C), N(pr.R * pr.C), K(N - 1), b(pr), rt(pr) {
        alpha = opt.getD("alpha", 3.0);
        M = (int)opt.getI("cand", 12);
        byK = (int)opt.getI("by-cands", 4);
        lambda = opt.getD("by-weight", 0.5);
        kappa = opt.getD("kappa", 4.0);
        estMode = (int)opt.getI("est", 1);
        avgC = (pr.cost[0] + pr.cost[1] + pr.cost[2] + pr.cost[3]) / 4.0;
        bdist.assign(N, 0);
        seen.assign(N, 0);
    }

    Moves run() {
        for (int t = 1; t <= K; t++)
            if (b.inPlace(t)) tryLock(t - 1);
        int iter = 0;
        while (!tm.expired()) {
            iter++;
            if (!step()) break;
        }
        logf(1, opt, "[greedy] %d steps, %zu moves, P=%d\n", iter, out.size(), b.placed());
        return out;
    }

private:
    const Problem& pr;
    const Options& opt;
    const Timer& tm;
    int R, C, N, K;
    Board b;
    Router rt;
    Moves out;
    double alpha;
    int M, byK;
    double lambda, kappa, avgC;
    int estMode;
    // "потенциал" фишки: сколько примерно можно выиграть, поставив её позже
    double potential(int t, int cell) const {
        double v = (double)TILE_VALUE - kappa * (double)wmd(cell, t - 1);
        return v > 0 ? v : 0;
    }
    std::vector<long long> bdist;
    std::vector<int> seen;
    int stamp = 0;

    bool freeCell(int r, int c) const {
        return r >= 0 && r < R && c >= 0 && c < C && !rt.locked[r * C + c];
    }
    long long wmd(int cell, int target) const {
        int dr = target / C - cell / C, dc = target % C - cell % C;
        long long h = dr > 0 ? dr * pr.cost[DOWN] : -dr * pr.cost[UP];
        h += dc > 0 ? dc * pr.cost[RIGHT] : -dc * pr.cost[LEFT];
        return h;
    }

    // связна ли свободная область после фиксации клетки q
    bool connectedWithout(int q) {
        int qr = q / C, qc = q % C;
        // локальная проверка по кольцу из 8 соседей
        static const int RR[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
        static const int CC[8] = {0, 1, 1, 1, 0, -1, -1, -1};
        bool f[8];
        for (int i = 0; i < 8; i++) f[i] = freeCell(qr + RR[i], qc + CC[i]);
        int runs = 0;
        for (int i = 0; i < 8; i++) {
            // начало серии свободных клеток, содержащей ортогонального соседа
            if (f[i] && !f[(i + 7) % 8]) {
                bool orth = false;
                for (int j = i; f[j % 8] && j < i + 8; j++)
                    if (j % 2 == 0) orth = true;
                if (orth) runs++;
            }
        }
        bool allFree = true;
        for (int i = 0; i < 8; i++) allFree &= f[i];
        if (allFree || runs <= 1) return true;
        // глобальная проверка обходом в ширину
        stamp++;
        int start = -1, total = 0;
        for (int c = 0; c < N; c++)
            if (!rt.locked[c] && c != q) {
                total++;
                if (start < 0) start = c;
            }
        if (start < 0) return true;
        std::vector<int> st{start};
        seen[start] = stamp;
        int cnt = 0;
        while (!st.empty()) {
            int u = st.back();
            st.pop_back();
            cnt++;
            for (int d = 0; d < 4; d++) {
                int v = b.nb(u, d);
                if (v < 0 || v == q || rt.locked[v] || seen[v] == stamp) continue;
                seen[v] = stamp;
                st.push_back(v);
            }
        }
        return cnt == total;
    }

    // штраф за фиксацию q: соседние целевые клетки, превращающиеся в тупики
    int deadEnds(int q) {
        int pen = 0;
        for (int d = 0; d < 4; d++) {
            int n = b.nb(q, d);
            if (n < 0 || rt.locked[n] || n == N - 1) continue;
            int fr = 0;
            for (int e = 0; e < 4; e++) {
                int m = b.nb(n, e);
                if (m >= 0 && m != q && !rt.locked[m]) fr++;
            }
            if (fr <= 1) pen++;
        }
        return pen;
    }

    bool tryLock(int q) {
        if (rt.locked[q] || b.blank == q) return false;
        if (!connectedWithout(q)) return false;
        rt.locked[q] = 1;
        return true;
    }

    void blankDistances() {
        typedef std::pair<long long, int> QE;
        std::priority_queue<QE, std::vector<QE>, std::greater<QE>> q;
        std::fill(bdist.begin(), bdist.end(), (long long)4e18);
        bdist[b.blank] = 0;
        q.push(QE(0, b.blank));
        while (!q.empty()) {
            QE e = q.top();
            q.pop();
            if (e.first != bdist[e.second]) continue;
            for (int d = 0; d < 4; d++) {
                int v = b.nb(e.second, d);
                if (v < 0 || rt.locked[v]) continue;
                long long nd = e.first + pr.blankCost(d);
                if (nd < bdist[v]) {
                    bdist[v] = nd;
                    q.push(QE(nd, v));
                }
            }
        }
    }

    bool step() {
        blankDistances();
        std::vector<std::pair<double, int>> cand;
        for (int t = 1; t <= K; t++) {
            int p = b.pos[t], q = t - 1;
            if (p == q || rt.locked[q] || bdist[p] >= (long long)4e18) continue;
            // быстрая оценка: по диагонали фишка идёт ~3 хода на шаг, по прямой ~5
            double est;
            if (estMode == 1) {
                int dx = std::abs(p % C - q % C), dy = std::abs(p / C - q / C);
                est = avgC * (5.0 * std::max(dx, dy) + std::min(dx, dy)) * alpha / 3.0 + (double)bdist[p];
            } else {
                est = alpha * (double)wmd(p, q) + (double)bdist[p];
            }
            double sc = (double)TILE_VALUE - est;
            if (sc > -2.0 * TILE_VALUE) cand.push_back({-sc, t});
        }
        if (cand.empty()) return false;
        std::sort(cand.begin(), cand.end());
        // точная стоимость проводки для лучших по быстрой оценке кандидатов
        std::vector<std::pair<long long, int>> feas;   // (-прибыль, фишка)
        int tried = 0;
        long long bestProfit = 0;
        for (auto& cv : cand) {
            if (tried >= M) break;
            int t = cv.second, q = t - 1;
            if (!connectedWithout(q)) continue;
            tried++;
            long long pen = deadEnds(q) * (TILE_VALUE / 2);
            long long limit = TILE_VALUE - pen;
            if (byK <= 1) limit -= bestProfit;
            if (limit <= 0) continue;
            long long c = rt.tileCost(b, t, q, limit);
            if (c < 0) continue;
            long long profit = TILE_VALUE - c - pen;
            if (profit > 0) feas.push_back({-profit, t});
            bestProfit = std::max(bestProfit, profit);
        }
        if (feas.empty()) return false;
        std::sort(feas.begin(), feas.end());
        // среди лучших -- учёт побочного эффекта: как маршрут сдвигает другие фишки
        int bestT = feas[0].second;
        Moves bestMv;
        double bestScore = -1e300;
        for (int i = 0; i < (int)feas.size() && i < std::max(1, byK); i++) {
            int t = feas[i].second;
            Board tmp = b;
            Moves mv;
            if (!rt.moveTile(tmp, t, t - 1, mv)) continue;
            double score = (double)(-feas[i].first);
            if (byK > 1 && lambda > 0) {
                Board pre = b;
                double dphi = 0;
                for (uint8_t m : mv) {
                    int from = pre.nb(pre.blank, m), to = pre.blank;
                    int u = pre.a[from];
                    pre.move(m);
                    if (u == t) continue;
                    dphi += potential(u, to) - potential(u, from);
                }
                score += lambda * dphi;
            }
            if (score > bestScore) {
                bestScore = score;
                bestT = t;
                bestMv = mv;
            }
        }
        if (bestMv.empty() && !b.inPlace(bestT)) return false;
        size_t before = out.size();
        Board pre = b;
        for (uint8_t m : bestMv) {
            b.move(m);
            out.push_back(m);
        }
        tryLock(bestT - 1);
        // фишки, случайно вставшие на места по дороге, тоже фиксируем
        for (size_t i = before; i < out.size(); i++) {
            int t = pre.move(out[i]);
            if (b.inPlace(t) && !rt.locked[t - 1]) tryLock(t - 1);
        }
        return true;
    }
};

}  // namespace

Moves solveGreedy(const Problem& pr, const Options& opt, const Timer& tm) {
    if (pr.allCostsZero()) return solveReduce(pr, opt, tm);
    // несколько прогонов с разными параметрами, пока есть время; лучший -- результат
    static const char* variants[][3] = {
        {"3", "12", "1"}, {"2", "12", "1"}, {"3", "8", "0"}, {"4", "16", "1"}, {"2.5", "12", "0"}, {"3", "20", "1"},
    };
    const int nv = (int)(sizeof(variants) / sizeof(variants[0]));
    const bool fixed = opt.has("alpha") || opt.has("cand") || opt.has("est");
    Moves best;
    long long bestF = scoreOfEmpty(pr).F;
    for (int v = 0; v < (fixed ? 1 : nv); v++) {
        Timer t0(0);
        Options o = opt;
        if (!fixed) {
            o.params["alpha"] = variants[v][0];
            o.params["cand"] = variants[v][1];
            o.params["est"] = variants[v][2];
        }
        Greedy g(pr, o, tm);
        Moves mv = polish(pr, g.run());
        Score sc;
        if (simulate(pr, mv, sc) && sc.F > bestF) {
            bestF = sc.F;
            best = mv;
        }
        logf(1, opt, "[greedy] variant %d: F = %lld (best %lld, %.2fs)\n", v, sc.F, bestF, tm.elapsed());
        if (tm.expired() || (tm.limit > 0 && t0.elapsed() * 1.2 > tm.remaining())) break;
    }
    return best;
}

}  // namespace p15
