// beam.cpp -- лучевой поиск (beam search) по ходам пустой клетки.
//
// На каждом слое из всех потомков текущих узлов оставляются W лучших по
// f = g + w*(h + b), где g -- стоимость пути, h = sum_t omega_t * md_t (+ LC)
// -- взвешенное манхэттенское расстояние, b -- расстояние пустой клетки до
// очередной фишки. Повторы позиций отсекаются хеш-таблицей.
//
// Два режима:
//  * глобальный (поля до 6x6): одна цель -- всё поле; веса omega_t больше
//    у фишек, которые при послойной сборке ставятся раньше; лучшее F
//    отслеживается по всем узлам (учитываются и частичные решения);
//  * послойный (большие поля): поле уменьшается как в reduce, но каждая строка
//    / столбец (кроме последних двух клеток) собирается лучевым поиском сразу
//    для всех её фишек; пара в конце -- точным поиском; остаток <= 25 клеток --
//    глобальным лучевым поиском (<= 16 клеток -- ещё и алгоритмом ida).
// Ширина луча растёт, пока есть время (anytime).
#include "solvers.hpp"
#include "stages.hpp"

namespace p15 {

namespace {

struct Spec {
    std::vector<uint8_t> allowed;   // клетки, куда может ходить пустая
    std::vector<double> omega;      // вес фишки в эвристике (множитель взвешенного расстояния)
    std::vector<double> kap;        // добавка за шаг (в единицах средней стоимости хода)
    std::vector<int> goal;          // послойный режим: фишки линии в порядке заполнения
    double wgt = 1.0;               // f = g + wgt * (h + b)
    double mu = 1.0;                // вес линейных конфликтов (глобальный режим)
    double beta = 0.0;              // вес расстояния пустой до очередной фишки
    double tau = 0.0;               // штраф за фишку линии, стоящую не по порядку
    int W = 100;
    long long maxDepth = 1000000;
};

struct Child {
    double f, h;
    long long g;
    uint64_t hash;
    int parent, move, P, gin, focus, focusPos, ooo;
    int sumD;   // оценка пути пустой между фишками, следующими за фокусом
};

struct Node {
    long long g = 0;
    double h = 0, f = 0;
    uint64_t hash = 0;
    int blank = 0, prev = -1, hist = -1, P = 0, gin = 0, focus = 0, focusPos = 0;
    int ooo = 0;   // фишки линии, стоящие на местах не по порядку (за "фокусом")
    int sumD = 0;  // sum_{i>focus} |цель(i-1) - позиция(i)|
};

struct Hist {
    int parent;
    uint8_t move;
};

class BeamSearch {
public:
    explicit BeamSearch(const Problem& pr) : pr(pr), R(pr.R), C(pr.C), N(pr.R * pr.C), K(N - 1) {
        avgCost = std::max(0.25, (double)(pr.cost[0] + pr.cost[1] + pr.cost[2] + pr.cost[3]) / 4.0);
        buf.resize(std::max(R, C) + 2);
        tails.resize(std::max(R, C) + 2);
        visited.assign((size_t)1 << 22, std::pair<uint64_t, long long>(0, 0));
        visGen.assign(visited.size(), 0);
    }

    // Глобальный режим (spec.goal пуст): возвращает путь к позиции с лучшим F
    // (bestF -- вход/выход). Послойный режим: путь к самому дешёвому узлу, где
    // все фишки spec.goal на местах; ok -- найден ли такой узел.
    Moves run(const Board& start, const Spec& spec, const Timer& tm, long long& bestF, bool& ok) {
        const bool stageMode = !spec.goal.empty();
        const int G = (int)spec.goal.size();
        goalIdx.assign(N, -1);
        for (int i = 0; i < G; i++) goalIdx[spec.goal[i]] = i;
        if (++curGen == 0) {   // переполнение счётчика поколений
            std::fill(visGen.begin(), visGen.end(), 0);
            curGen = 1;
        }
        const size_t ttMask = visited.size() - 1;
        hist.clear();
        ok = false;

        cur.assign(1, Node());
        boards.assign((size_t)N, 0);
        for (int i = 0; i < N; i++) boards[i] = (uint16_t)start.a[i];
        Node& root = cur[0];
        root.blank = start.blank;
        root.P = start.placed();
        for (int c = 0; c < N; c++) root.hash ^= zkey(c, start.a[c]);
        for (int t = 1; t <= K; t++)
            if (spec.omega[t] != 0 || (!spec.kap.empty() && spec.kap[t] != 0))
                root.h += tileH(spec, t, start.pos[t]);
        if (spec.mu > 0) root.h += spec.mu * (double)totalLC(&boards[0]);
        root.gin = 0;
        for (int t : spec.goal)
            if (start.pos[t] == t - 1) root.gin++;
        root.focus = 0;
        while (root.focus < G && start.pos[spec.goal[root.focus]] == spec.goal[root.focus] - 1) root.focus++;
        root.focusPos = root.focus < G ? start.pos[spec.goal[root.focus]] : 0;
        root.ooo = 0;
        root.sumD = 0;
        for (int i = root.focus + 1; i < G; i++) {
            if (start.pos[spec.goal[i]] == spec.goal[i] - 1) root.ooo++;
            root.sumD += mdist(spec.goal[i - 1] - 1, start.pos[spec.goal[i]]);
        }
        if (stageMode && root.gin == G) {
            ok = true;
            return Moves();
        }

        long long localBest = TILE_VALUE * root.P;
        int bestHist = -2;
        long long bestGoalG = (long long)4e18;
        int bestGoalHist = -1;
        int partFocus = root.focus, partHist = -1;
        double partF = 1e300;
        if (!stageMode && localBest > bestF) bestF = localBest;
        const long long fullValue = TILE_VALUE * K;
        std::vector<Child> ch;
        std::vector<uint16_t> nboards;
        exitReason = 0;
        truncated = false;
        long long depth = 0;
        for (depth = 0; depth < spec.maxDepth; depth++) {
            if ((depth & 7) == 0 && tm.expired()) { exitReason = 1; break; }
            if (hist.size() > 25000000) { exitReason = 2; break; }
            ch.clear();
            long long minG = (long long)4e18;
            for (size_t i = 0; i < cur.size(); i++) {
                const Node& nd = cur[i];
                minG = std::min(minG, nd.g);
                const uint16_t* a = &boards[i * N];
                int br = nd.blank / C, bc = nd.blank % C;
                for (int bd = 0; bd < 4; bd++) {
                    if (bd == (nd.prev ^ 1)) continue;
                    int nr = br + DROW[bd], nc = bc + DCOL[bd];
                    if (nr < 0 || nr >= R || nc < 0 || nc >= C) continue;
                    int n = nr * C + nc;
                    if (!spec.allowed[n]) continue;
                    int t = a[n];
                    Child c;
                    c.g = nd.g + pr.blankCost(bd);
                    c.P = nd.P - (n == t - 1) + (nd.blank == t - 1);
                    c.hash = nd.hash ^ zkey(n, t) ^ zkey(nd.blank, t) ^ zkey(n, 0) ^ zkey(nd.blank, 0);
                    double dh = 0;
                    if (spec.omega[t] != 0 || (!spec.kap.empty() && spec.kap[t] != 0)) {
                        dh = tileH(spec, t, nd.blank) - tileH(spec, t, n);
                        if (spec.mu > 0) dh += spec.mu * (double)lcDelta(a, t, n, nd.blank, bd);
                    }
                    c.h = nd.h + dh;
                    c.gin = nd.gin;
                    c.focus = nd.focus;
                    c.focusPos = nd.focusPos;
                    c.ooo = nd.ooo;
                    c.sumD = nd.sumD;
                    int gi = goalIdx[t];
                    if (gi >= 0) {
                        bool wasIn = n == t - 1, nowIn = nd.blank == t - 1;
                        c.gin += (int)nowIn - (int)wasIn;
                        if (gi == nd.focus) {
                            c.focusPos = nd.blank;
                            if (nowIn) {   // фокусная фишка встала: следующий фокус
                                int k = gi + 1;
                                while (k < G) {
                                    int tc = spec.goal[k] - 1;
                                    int v = tc == n ? 0 : (tc == nd.blank ? t : a[tc]);
                                    if (v != spec.goal[k]) break;
                                    c.sumD -= 1;   // соседние клетки линии
                                    k++;
                                    c.ooo--;
                                }
                                c.focus = k;
                                if (k < G) {
                                    for (int x = 0; x < N; x++)
                                        if (a[x] == spec.goal[k]) { c.focusPos = x; break; }
                                    c.sumD -= mdist(spec.goal[k - 1] - 1, c.focusPos);
                                }
                            }
                        } else if (gi < nd.focus) {   // сдвинута уже поставленная фишка
                            c.focus = gi;
                            c.focusPos = nd.blank;
                            c.ooo += nd.focus - gi - 1;
                            c.sumD += nd.focus - gi - 1;
                            if (nd.focus < G) c.sumD += mdist(spec.goal[nd.focus - 1] - 1, nd.focusPos);
                        } else {                      // фишка за фокусом
                            c.ooo += (int)nowIn - (int)wasIn;
                            int tg = spec.goal[gi - 1] - 1;
                            c.sumD += mdist(tg, nd.blank) - mdist(tg, n);
                        }
                    }
                    double bterm = 0;
                    if (spec.beta > 0 && c.focus < G)
                        bterm = spec.beta * (pushDist(n, c.focusPos, spec.goal[c.focus] - 1, spec.allowed) + c.sumD);
                    // фишки, поставленные не по порядку, создают "карманы" -- штраф
                    c.f = (double)c.g + spec.wgt * (c.h + bterm) + spec.tau * c.ooo;
                    c.parent = (int)i;
                    c.move = bd;
                    ch.push_back(c);
                }
            }
            if (ch.empty()) { exitReason = 3; break; }
            if (stageMode) {
                if (minG >= bestGoalG) { exitReason = 4; break; }
                for (const Child& c : ch)
                    if (c.gin == G && c.g < bestGoalG) {
                        bestGoalG = c.g;
                        hist.push_back({cur[c.parent].hist, (uint8_t)c.move});
                        bestGoalHist = (int)hist.size() - 1;
                    }
            } else {
                if (fullValue - minG <= std::max(bestF, localBest)) break;
                for (const Child& c : ch) {
                    long long F = TILE_VALUE * c.P - c.g;
                    if (F > localBest) {
                        localBest = F;
                        hist.push_back({cur[c.parent].hist, (uint8_t)c.move});
                        bestHist = (int)hist.size() - 1;
                    }
                }
            }
            // повторы позиций: в слое и глобально
            std::sort(ch.begin(), ch.end(), [](const Child& x, const Child& y) {
                return x.hash != y.hash ? x.hash < y.hash : x.g < y.g;
            });
            size_t m = 0;
            for (size_t i = 0; i < ch.size(); i++) {
                if (i > 0 && ch[i].hash == ch[i - 1].hash) continue;
                if (stageMode && ch[i].gin == G) continue;   // цель достигнута -- не продолжаем
                size_t vi = ch[i].hash & ttMask;
                auto& e = visited[vi];
                if (visGen[vi] == curGen && e.first == ch[i].hash && e.second <= ch[i].g) continue;
                ch[m++] = ch[i];
            }
            ch.resize(m);
            if (ch.empty()) { exitReason = 5; break; }
            if ((int)ch.size() > spec.W) {
                truncated = true;
                std::nth_element(ch.begin(), ch.begin() + spec.W, ch.end(),
                                 [](const Child& x, const Child& y) { return x.f < y.f; });
                ch.resize(spec.W);
            }
            nboards.resize(ch.size() * (size_t)N);
            std::vector<Node> nxt(ch.size());
            for (size_t i = 0; i < ch.size(); i++) {
                const Child& c = ch[i];
                const Node& p = cur[c.parent];
                uint16_t* dst = &nboards[i * N];
                const uint16_t* src = &boards[(size_t)c.parent * N];
                std::copy(src, src + N, dst);
                int n = p.blank + (c.move == UP ? -C : c.move == DOWN ? C : c.move == LEFT ? -1 : 1);
                dst[p.blank] = dst[n];
                dst[n] = 0;
                Node& q = nxt[i];
                q.g = c.g;
                q.h = c.h;
                q.f = c.f;
                q.hash = c.hash;
                q.blank = n;
                q.prev = c.move;
                q.P = c.P;
                q.gin = c.gin;
                q.focus = c.focus;
                q.focusPos = c.focusPos;
                q.ooo = c.ooo;
                q.sumD = c.sumD;
                hist.push_back({p.hist, (uint8_t)c.move});
                q.hist = (int)hist.size() - 1;
                if (stageMode && (q.focus > partFocus || (q.focus == partFocus && partHist >= 0 && q.f < partF))) {
                    partFocus = q.focus;
                    partF = q.f;
                    partHist = q.hist;
                }
                size_t vi = c.hash & ttMask;
                visited[vi].first = c.hash;
                visited[vi].second = c.g;
                visGen[vi] = curGen;
            }
            cur.swap(nxt);
            boards.swap(nboards);
        }
        if (depth >= spec.maxDepth) exitReason = 6;
        lastDepth = depth;
        Moves mv;
        int hi = stageMode ? bestGoalHist : bestHist;
        if (stageMode) {
            ok = bestGoalHist >= 0;
            if (!ok) hi = partHist;   // цель не достигнута: лучший частичный результат
        }
        if (hi >= 0)
            for (int i = hi; i >= 0; i = hist[i].parent) mv.push_back(hist[i].move);
        std::reverse(mv.begin(), mv.end());
        if (!stageMode && localBest > bestF) bestF = localBest;
        return mv;
    }

private:
    const Problem& pr;
    int R, C, N, K;
    std::vector<int> goalIdx;
    std::vector<Node> cur;
    std::vector<uint16_t> boards;
    std::vector<Hist> hist;
    std::vector<std::pair<uint64_t, long long>> visited;
    std::vector<uint32_t> visGen;   // поколение записи (очистка таблицы -- O(1))
    uint32_t curGen = 0;
    std::vector<int> buf, tails;
public:
    int exitReason = 0;
    long long lastDepth = 0;
    bool truncated = false;   // луч хоть раз обрезался (иначе поиск был полным)
private:

    // станет ли после установки фишки goal[gi] (пришедшей из клетки from) одна из
    // соседних по линии незаполненных клеток зажатой с двух сторон
    bool makesPocket(const uint16_t* a, const std::vector<int>& goal, int gi, int from) const {
        const int G = (int)goal.size();
        auto placedAt = [&](int j) {
            int cell = goal[j] - 1;
            if (j == gi) return true;
            int v = a[cell];
            if (cell == from) v = 0;
            return v == goal[j];
        };
        for (int j = gi - 1; j <= gi + 1; j += 2) {
            if (j < 0 || j >= G || placedAt(j)) continue;
            bool lb = (j == 0) || placedAt(j - 1);
            bool rb = (j + 1 < G) && placedAt(j + 1);
            if (lb && rb) return true;
        }
        return false;
    }
    int mdist(int a, int b) const { return std::abs(a / C - b / C) + std::abs(a % C - b % C); }
    double tileH(const Spec& sp, int t, int cell) const {
        double v = sp.omega[t] * (double)wmd(cell, t - 1);
        if (!sp.kap.empty() && sp.kap[t] != 0) v += sp.kap[t] * avgCost * (double)mdist(cell, t - 1);
        return v;
    }
    double avgCost = 1.0;
    // расстояние от пустой клетки до клетки, откуда можно толкнуть фишку к цели
    int pushDist(int blank, int tile, int target, const std::vector<uint8_t>& allowed) const {
        int br = blank / C, bc = blank % C, tr = tile / C, tc = tile % C;
        int gr = target / C, gc = target % C;
        int best = 1 << 29;
        for (int d = 0; d < 4; d++) {
            bool need = (d == UP && gr < tr) || (d == DOWN && gr > tr) || (d == LEFT && gc < tc) ||
                        (d == RIGHT && gc > tc);
            if (!need) continue;
            int pr_ = tr + DROW[d], pc = tc + DCOL[d];
            if (!allowed[pr_ * C + pc]) continue;
            int dist = std::abs(br - pr_) + std::abs(bc - pc);
            bool blocked = (d == UP && bc == tc && br > tr) || (d == DOWN && bc == tc && br < tr) ||
                           (d == LEFT && br == tr && bc > tc) || (d == RIGHT && br == tr && bc < tc);
            if (blocked) dist += 2;
            best = std::min(best, dist);
        }
        return best == (1 << 29) ? 0 : best;
    }
    long long wmd(int cell, int target) const {
        int dr = target / C - cell / C, dc = target % C - cell % C;
        long long h = dr > 0 ? dr * pr.cost[DOWN] : -dr * pr.cost[UP];
        h += dc > 0 ? dc * pr.cost[RIGHT] : -dc * pr.cost[LEFT];
        return h;
    }
    int lis(int n) {
        int len = 0;
        for (int i = 0; i < n; i++) {
            int x = buf[i], lo = 0, hi = len;
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
    // конфликты линии с учётом гипотетического хода фишки t из from в to
    long long lineLC(const uint16_t* a, bool isRow, int idx, int t, int from, int to) {
        int n = 0;
        int L = isRow ? C : R;
        for (int i = 0; i < L; i++) {
            int cell = isRow ? idx * C + i : i * C + idx;
            int v = a[cell];
            if (cell == from) v = 0;
            if (cell == to) v = t;
            if (!v) continue;
            int tr = (v - 1) / C, tc = (v - 1) % C;
            if (isRow ? tr == idx : tc == idx) buf[n++] = isRow ? tc : tr;
        }
        if (n < 2) return 0;
        return (long long)(n - lis(n)) * (isRow ? pr.cost[UP] + pr.cost[DOWN] : pr.cost[LEFT] + pr.cost[RIGHT]);
    }
    long long lcDelta(const uint16_t* a, int t, int from, int to, int bd) {
        if (bd == UP || bd == DOWN) {
            int gr = (t - 1) / C;
            if (gr != from / C && gr != to / C) return 0;
            return lineLC(a, true, gr, t, from, to) - lineLC(a, true, gr, 0, -1, -1);
        }
        int gc = (t - 1) % C;
        if (gc != from % C && gc != to % C) return 0;
        return lineLC(a, false, gc, t, from, to) - lineLC(a, false, gc, 0, -1, -1);
    }
    long long totalLC(const uint16_t* a) {
        long long s = 0;
        for (int r = 0; r < R; r++) s += lineLC(a, true, r, 0, -1, -1);
        for (int c = 0; c < C; c++) s += lineLC(a, false, c, 0, -1, -1);
        return s;
    }
};

// номер этапа послойной сборки для каждой клетки (как в reduce, режим 0)
std::vector<int> stageOfCells(int R, int C, int& stages) {
    std::vector<int> stage(R * C, 0);
    int r0 = 0, c0 = 0, s = 0;
    while ((R - r0) * (C - c0) > 9) {
        int h = R - r0, w = C - c0;
        bool isRow = (h == 2) ? false : (w == 2) ? true : (h >= w);
        if (isRow) {
            for (int c = c0; c < C; c++) stage[r0 * C + c] = s;
            r0++;
        } else {
            for (int r = r0; r < R; r++) stage[r * C + c0] = s;
            c0++;
        }
        s++;
    }
    for (int r = r0; r < R; r++)
        for (int c = c0; c < C; c++) stage[r * C + c] = s;
    stages = s;
    return stage;
}

// глобальный лучевой поиск на всём поле задачи pr
Moves globalBeam(const Problem& pr, const Options& opt, const Timer& tm, int W, long long& bestF,
                 bool* truncated = nullptr) {
    const int N = pr.cells();
    BeamSearch bs(pr);
    Spec sp;
    sp.allowed.assign(N, 1);
    int stages = 0;
    std::vector<int> stage = stageOfCells(pr.R, pr.C, stages);
    double lambda = opt.getD("lambda", N <= 16 ? 0.0 : 0.5);
    sp.omega.assign(N, 1.0);
    for (int t = 1; t < N; t++) sp.omega[t] = 1.0 + lambda * (stages - stage[t - 1]);
    sp.wgt = opt.getD("w", 1.0);
    sp.mu = opt.getD("mu", 1.0);
    sp.W = W;
    bool ok;
    Board b(pr);
    Moves mv = bs.run(b, sp, tm, bestF, ok);
    if (truncated) *truncated = bs.truncated;
    return mv;
}

// послойный режим
Moves stagedBeam(const Problem& pr, const Options& opt, const Timer& tm, int W, long long bestF) {
    const int R = pr.R, C = pr.C, N = R * C;
    const int endCells = (int)opt.getI("end-cells", 25);
    const bool useStageBeam = opt.getI("stage-beam", 1) != 0;
    BeamSearch bs(pr);
    Board b(pr);
    Router rt(pr);
    Moves out;
    long long cost = 0;
    int r0 = 0, c0 = 0;
    const int K = N - 1;
    while ((R - r0) * (C - c0) > endCells) {
        if (tm.expired()) return out;
        int h = R - r0, w = C - c0;
        bool isRow = (h == 2) ? false : (w == 2) ? true : (h >= w);
        std::vector<int> L = lineCells(pr, r0, c0, isRow, false);
        int k = (int)L.size(), in = isRow ? DOWN : RIGHT, depth = std::min(3, isRow ? h : w);
        size_t before = out.size();
        if (k > 2 && !useStageBeam) {
            for (int i = 0; i + 2 < k; i++) {
                if (!rt.moveTile(b, L[i] + 1, L[i], out)) return out;
                rt.locked[L[i]] = 1;
            }
        } else if (k > 2) {
            Spec sp;
            sp.allowed.assign(N, 0);
            for (int r = r0; r < R; r++)
                for (int c = c0; c < C; c++) sp.allowed[r * C + c] = 1;
            // эвристика этапа: фишки линии -- (стоимость хода + kappa * средняя стоимость) за шаг,
            // остальные фишки области -- omega-rest * стоимость шага (чтобы переходы пустой
            // клетки были "полезными"), плюс путь пустой до очередной фишки
            const double avg = std::max(0.25, (double)(pr.cost[0] + pr.cost[1] + pr.cost[2] + pr.cost[3]) / 4.0);
            sp.omega.assign(N, 0.0);
            sp.kap.assign(N, 0.0);
            const double orest = opt.getD("omega-rest", 1.0);
            if (orest > 0)
                for (int r = r0; r < R; r++)
                    for (int c = c0; c < C; c++)
                        if (r * C + c != N - 1) sp.omega[r * C + c + 1] = orest;
            const double kg = opt.getD("kappa-goal", 3.0);
            for (int i = 0; i + 2 < k; i++) {
                sp.goal.push_back(L[i] + 1);
                sp.omega[L[i] + 1] = 1.0;
                sp.kap[L[i] + 1] = kg;
            }
            for (int j = k - 2; j < k; j++) {
                sp.omega[L[j] + 1] = 1.0;
                sp.kap[L[j] + 1] = opt.getD("kappa-pair", 1.0);
            }
            sp.wgt = opt.getD("sw", 1.0);
            sp.mu = 0;
            sp.beta = opt.getD("beta", 1.0) * avg;
            sp.W = W;
            sp.tau = opt.getD("tau", 5.0) * avg;
            long long mdSum = 0;
            for (int t : sp.goal) mdSum += std::abs(b.pos[t] / C - (t - 1) / C) + std::abs(b.pos[t] % C - (t - 1) % C);
            sp.maxDepth = 200 + (long long)(opt.getD("depth-mult", 30.0) * mdSum);
            long long dummy = 0;
            bool ok = false;
            // ограничение по времени на этап
            int stagesLeft = std::max(1, (h + w) - 6);
            Timer st(tm.limit > 0 ? std::max(0.05, tm.remaining() / (stagesLeft + 1)) : 0);
            Timer both(std::min(st.limit > 0 ? st.limit : 1e18, tm.limit > 0 ? tm.remaining() : 1e18));
            Timer stt(0);
            Moves mv = bs.run(b, sp, both, dummy, ok);
            logf(2, opt, "[beam]   stage r0=%d c0=%d %s: ok=%d moves=%zu (%.3fs) exit=%d depth=%lld/%lld\n", r0, c0,
                 isRow ? "row" : "col", (int)ok, mv.size(), stt.elapsed(), bs.exitReason, bs.lastDepth, sp.maxDepth);
            applyMoves(b, mv, out);
            if (ok) {
                for (int i = 0; i + 2 < k; i++) rt.locked[L[i]] = 1;
            } else {   // запасной вариант: оставшиеся фишки -- по одной
                for (int i = 0; i + 2 < k; i++) {
                    if (!rt.moveTile(b, L[i] + 1, L[i], out)) return out;
                    rt.locked[L[i]] = 1;
                }
            }
        }
        if (!placePair(pr, b, rt, L, in, depth, out)) {
            logf(2, opt, "[beam]   placePair failed at r0=%d c0=%d\n", r0, c0);
            return out;
        }
        for (size_t i = before; i < out.size(); i++) cost += pr.blankCost(out[i]);
        if (isRow) r0++;
        else c0++;
        // если даже идеальное продолжение не догонит рекорд -- прекращаем
        long long Fnow = TILE_VALUE * b.placed() - cost;
        if (Fnow + TILE_VALUE * (K - b.placed()) <= bestF) return out;
    }
    // остаток
    Problem sub;
    if (!makeSubProblem(pr, b, r0, c0, sub)) return out;
    Options o2 = opt;
    o2.verbose = 0;
    Moves tail;
    int sn = sub.cells();
    if (sn <= 9) {
        tail = solveExact(sub, o2, Timer(0));
    } else {
        // остаток: лучшее из быстрой послойной сборки и глобального лучевого поиска
        double lim = tm.limit > 0 ? std::max(0.2, tm.remaining() * 0.3) : 30.0;
        Options of = o2;
        of.params["runs"] = "2";
        of.params["ida-endgame"] = "0";
        tail = solveReduce(sub, of, Timer(std::min(2.0, lim * 0.1)));
        Score s1, s2;
        simulate(sub, tail, s1);
        long long sbF = s1.F;
        Moves gb = globalBeam(sub, o2, Timer(lim), std::max(2000, 4 * W), sbF);
        if (simulate(sub, gb, s2) && s2.F > s1.F) {
            tail = gb;
            s1 = s2;
        }
        if (sn <= 16) {
            Moves im = idaSearch(sub, tail, std::max(0.2, lim * 0.5), o2).moves;
            if (simulate(sub, im, s2) && s2.F > s1.F) tail = im;
        }
    }
    applyMoves(b, tail, out);
    return out;
}

}  // namespace

Moves solveBeam(const Problem& pr, const Options& opt, const Timer& tm) {
    if (pr.allCostsZero()) return solveReduce(pr, opt, tm);
    const int N = pr.cells();
    if (N > 65000) throw NotApplicable("board is too large for beam search");
    long long bestF = scoreOfEmpty(pr).F;
    Moves best;
    // целиком -- только небольшие "квадратные" поля; вытянутые и большие -- послойно
    const bool staged = opt.getI("staged", (pr.R <= 6 && pr.C <= 6) ? 0 : 1) != 0;
    long long W = opt.getI("width", 0);
    long long maxCells = opt.getI("max-cells", 6000000);   // ограничение памяти слоя
    long long w = W > 0 ? W : (staged && N > 900 ? 128 : 64);
    bool complete = false;
    while (true) {
        long long wcap = std::max<long long>(1, maxCells / N);
        long long ww = std::min(w, wcap);
        Timer t0(0);
        Moves mv;
        if (staged) {
            mv = stagedBeam(pr, opt, tm, (int)ww, std::max(bestF, (long long)opt.getI("stop-below", bestF)));
        } else {
            long long bf = bestF;
            bool tr = true;
            mv = globalBeam(pr, opt, tm, (int)ww, bf, &tr);
            if (!tr) complete = true;   // луч ни разу не обрезался -- расширять бессмысленно
        }
        Score sc;
        mv = polish(pr, mv);
        if (simulate(pr, mv, sc) && sc.F > bestF) {
            best = mv;
            bestF = sc.F;
        }
        logf(1, opt, "[beam] %s width %lld: F = %lld, best F = %lld (%.2fs)\n", staged ? "staged" : "global", ww,
             sc.F, bestF, tm.elapsed());
        if (W > 0 || tm.expired() || ww >= wcap || complete) break;
        // следующий прогон -- в 3 раза шире, если по прогнозу успевает
        if (tm.limit > 0 && t0.elapsed() * (staged ? 4.5 : 3.5) > tm.remaining()) break;
        w = ww * (staged ? 4 : 3);
    }
    return best;
}

}  // namespace p15
