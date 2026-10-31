// best.cpp -- портфель: запускает подходящие для данного поля алгоритмы,
// делит между ними время и возвращает лучшее решение.
//
//  * <= 10 клеток        : exact (точный оптимум);
//  * позиция близка к собранной : ida, затем reduce и beam (при любом размере);
//  * <= 25 клеток        : reduce -> beam -> ida (с рекордом от beam) -> beam;
//  * до 2500 клеток      : greedy (быстро), reduce (быстро), beam -- основное время;
//  * больше              : greedy (полная сборка там заведомо убыточна).
#include <cstdlib>

#include "solvers.hpp"

namespace p15 {

namespace {

// число фишек не на своих местах
int misplaced(const Problem& pr) {
    int m = 0;
    for (int i = 0; i + 1 < pr.cells(); i++)
        if (pr.start[i] != i + 1) m++;
    return m;
}

// сумма манхэттенских расстояний фишек до своих мест
long long manhattan(const Problem& pr) {
    long long s = 0;
    for (int i = 0; i < pr.cells(); i++) {
        int t = pr.start[i];
        if (t == 0) continue;
        s += std::abs(i / pr.C - (t - 1) / pr.C) + std::abs(i % pr.C - (t - 1) % pr.C);
    }
    return s;
}

}  // namespace

Moves solveBest(const Problem& pr, const Options& opt, const Timer& tm) {
    const int N = pr.cells();
    const double total = tm.limit > 0 ? tm.limit : 60.0;
    Timer clock(total);
    long long bestF = scoreOfEmpty(pr).F;
    Moves best;
    std::string bestName = "none";
    Options q = opt;
    q.verbose = std::max(0, opt.verbose - 1);

    auto consider = [&](const std::string& name, const Moves& mv) {
        Moves pm = polish(pr, mv);
        Score sc;
        if (!simulate(pr, pm, sc)) return;
        logf(1, opt, "[best] %-8s F = %lld (%.2fs)\n", name.c_str(), sc.F, clock.elapsed());
        if (sc.F > bestF) {
            bestF = sc.F;
            best = pm;
            bestName = name;
        }
    };
    // доля оставшегося времени
    auto slice = [&](double frac) { return std::max(0.05, clock.remaining() * frac); };
    auto run = [&](const std::string& name, SolverFn fn, double frac, const Options& o) {
        if (clock.remaining() < 0.05) return;
        try {
            consider(name, fn(pr, o, Timer(slice(frac))));
        } catch (const std::exception& e) {
            logf(1, opt, "[best] %s failed: %s\n", name.c_str(), e.what());
        }
    };
    auto runIda = [&](double frac) {
        if (clock.remaining() < 0.05) return false;
        IdaResult r = idaSearch(pr, best, slice(frac), q);
        consider(r.optimal ? "ida*" : "ida", r.moves);
        return r.optimal;
    };

    if (N <= 10) {
        consider("exact", solveExact(pr, q, Timer(0)));
        return best;
    }
    if (pr.allCostsZero()) {   // F = 100*P при любом числе ходов
        run("reduce", solveReduce, 1.0, q);
        return best;
    }
    Options fast = q;
    fast.params["runs"] = "2";
    fast.params["ida-endgame"] = "0";
    const bool nearGoal = misplaced(pr) <= std::max(12, N / 25) || manhattan(pr) <= 60;

    auto withStop = [&](Options o) {   // рекорд передаётся дальше: заведомо проигрышная
        o.params["stop-below"] = std::to_string(bestF);   // полная сборка бросается
        return o;
    };
    if (nearGoal) {   // беспорядок локален: точный поиск, затем послойные методы (они быстры)
        if (runIda(0.3)) return best;
        run("reduce", solveReduce, 0.15, withStop(fast));
        run("beam", solveBeam, 0.7, withStop(q));
        runIda(1.0);
    } else if (N <= 25) {
        run("reduce", solveReduce, 0.05, fast);
        run("beam", solveBeam, 0.3, q);
        if (runIda(0.8)) return best;
        run("beam", solveBeam, 1.0, q);
    } else if (N <= 2500) {
        {   // один быстрый прогон greedy: при дорогих ходах частичная сборка бывает лучше полной
            Options g1 = q;
            g1.params["alpha"] = "3";
            run("greedy", solveGreedy, 0.15, g1);
        }
        run("reduce", solveReduce, 0.05, withStop(fast));
        run("beam", solveBeam, 0.9, withStop(q));   // на средних полях beam -- основной
        run("reduce", solveReduce, 1.0, withStop(q));
    } else {
        run("greedy", solveGreedy, 1.0, q);
    }
    logf(1, opt, "[best] winner: %s, F = %lld\n", bestName.c_str(), bestF);
    return best;
}

}  // namespace p15
