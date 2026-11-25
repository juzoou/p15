// Соревновательный режим: каждый кандидат проверяется на исходном поле.
// Частичный маршрут можно использовать как начало следующей попытки.
#include "solvers.hpp"

namespace p15 {
namespace {

// Удаляем уже собранные верхние строки и левые столбцы. Пустая клетка
// и все несовпадения остаются внутри. Ходы пустой не требуют перенумерации.
Problem activeCorner(const Problem& pr, const Board& b) {
    int r0 = pr.R - 2, c0 = pr.C - 2;
    for (int i = 0; i < b.N; ++i) {
        if (b.a[i] != (i + 1) % b.N) {
            r0 = std::min(r0, i / pr.C);
            c0 = std::min(c0, i % pr.C);
        }
    }
    Problem sub;
    if (!makeSubProblem(pr, b, r0, c0, sub)) throw std::logic_error("inconsistent active rectangle");
    return sub;
}

class CompleteRoutes {
public:
    CompleteRoutes(const Problem& problem, const Options& options, const Timer& clock)
        : pr(problem), opt(options), tm(clock) {}

    void consider(const Moves& moves) {
        Score score;
        if (!simulate(pr, moves, score) || score.P != pr.tiles()) return;
        Moves path = polish(pr, moves);
        if (!simulate(pr, path, score) || score.P != pr.tiles()) return;
        if (bestCost < 0 || score.cost < bestCost) {
            logf(1, opt, "[goal] complete route: cost %lld, moves %zu\n", score.cost, path.size());
            bestCost = score.cost;
            best = std::move(path);
        }
    }

    // Легальные промежуточные ходы продолжаем послойной сборкой.
    void finish(const Moves& prefix, double seconds) {
        if (tm.expired() || seconds <= 0) return;
        Board b;
        Score score;
        if (!simulate(pr, prefix, score, &b)) return;
        if (score.P == pr.tiles()) { consider(prefix); return; }
        if (bestCost >= 0 && score.cost >= bestCost) return;
        Problem sub = activeCorner(pr, b);
        Options o = opt;
        o.params["runs"] = "2";
        o.params["ida-endgame"] = "0";
        o.params.erase("stop-below");   // рекорд полного поля не является рекордом подзадачи
        Moves path = prefix;
        Moves tail = solveReduce(sub, o, Timer(seconds));
        path.insert(path.end(), tail.begin(), tail.end());
        consider(path);
    }

    Moves best;
    long long bestCost = -1;

private:
    const Problem& pr;
    const Options& opt;
    const Timer& tm;
};

}  // namespace

Moves solveForGoal(const Problem& pr, const Options& opt, const Timer& tm, const std::string& algorithm) {
    const int exactLimit = (int)std::clamp(opt.getI("exact-max-cells", 10), 0LL, 12LL);
    if ((algorithm == "exact" || algorithm == "best") && pr.cells() <= exactLimit) return solveExact(pr, opt, tm);
    if (algorithm == "reduce") return solveReduce(pr, opt, tm);

    CompleteRoutes routes(pr, opt, tm);
    // Сначала сохраняем полное решение. Для exact на больших полях это
    // основной алгоритм: конструктивное уменьшение + Дейкстра на остатке.
    routes.finish({}, algorithm == "exact" ? tm.remaining() : tm.remaining() * 0.3);
    if (algorithm == "exact" || tm.expired() || pr.allCostsZero()) return routes.best;

    Problem sub = activeCorner(pr, Board(pr));
    if (algorithm == "ida" || (algorithm == "best" && sub.cells() <= 16)) {
        // IDA* получает уже известное полное решение как верхнюю границу стоимости.
        // Начальная послойная сборка тоже выполнялась внутри этого прямоугольника.
        if (sub.cells() > 36) {
            Options endgame = opt;
            endgame.params["runs"] = "2";
            endgame.params["ida-endgame"] = "1";
            routes.consider(solveReduce(sub, endgame, Timer(tm.remaining())));
            return routes.best;
        }
        IdaResult result = idaSearch(sub, routes.best, tm.remaining(), opt);
        routes.consider(result.moves);
        return routes.best;
    }
    if (algorithm == "greedy") {
        Moves prefix = solveGreedy(sub, opt, Timer(tm.remaining() * 0.35));
        routes.finish(prefix, tm.remaining());
        return routes.best;
    }

    // beam и портфель best. На больших полях глобальный луч восстанавливает
    // длинные цепочки локальных сдвигов. Послойный алгоритм завершает остаток.
    if (sub.cells() > 36 && !opt.has("width") && !opt.has("staged")) {
        Moves prefix;
        for (int pass = 0; pass < 4 && !tm.expired(); ++pass) {
            Board b;
            Score before;
            if (!simulate(pr, prefix, before, &b) || before.P == pr.tiles()) break;
            Problem active = activeCorner(pr, b);
            double budget = std::min(1.5, tm.remaining() * 0.10);
            if (budget <= 0) break;
            Moves next = improveBeamPrefix(active, opt, Timer(budget), pass == 0 ? 64 : 96);
            if (next.empty()) break;
            prefix.insert(prefix.end(), next.begin(), next.end());
            routes.finish(prefix, tm.remaining() * 0.2);
        }
        if (!prefix.empty() && !tm.expired()) {
            Board b;
            Score score;
            if (simulate(pr, prefix, score, &b) && score.P != pr.tiles()) {
                Problem rest = activeCorner(pr, b);
                Options o = opt;
                if (routes.bestCost >= 0) o.params["stop-below"] = std::to_string(TILE_VALUE * rest.tiles() - routes.bestCost + score.cost);
                Moves tail = solveBeam(rest, o, Timer(tm.remaining() * 0.40));
                prefix.insert(prefix.end(), tail.begin(), tail.end());
                routes.consider(prefix);
            }
        }
    }
    if (!tm.expired()) {
        Options o = opt;
        if (routes.bestCost >= 0) o.params["stop-below"] = std::to_string(TILE_VALUE * sub.tiles() - routes.bestCost);
        routes.consider(solveBeam(sub, o, Timer(tm.remaining())));
    }
    return routes.best;
}

}  // namespace p15
