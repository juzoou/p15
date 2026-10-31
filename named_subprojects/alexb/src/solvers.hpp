// solvers.hpp -- объявления алгоритмов решения.
#pragma once
#include <algorithm>

#include "common.hpp"

namespace p15 {

// exact  -- Дейкстра по всем позициям, оптимум F (поля до 10 клеток)
Moves solveExact(const Problem& pr, const Options& opt, const Timer& tm);
// ida    -- IDA* / ветви и границы по F с аддитивными БД образцов (малые поля
//           и позиции, близкие к собранной, любого размера)
Moves solveIda(const Problem& pr, const Options& opt, const Timer& tm);
// reduce -- послойная сборка (строка/столбец) с оптимальной проводкой фишек
//           с учётом стоимостей и точным доигрыванием остатка
Moves solveReduce(const Problem& pr, const Options& opt, const Timer& tm);
// greedy -- частичная сборка: жадно ставит только выгодные фишки (большие поля)
Moves solveGreedy(const Problem& pr, const Options& opt, const Timer& tm);
// beam   -- лучевой поиск по ходам с послойной эвристикой (средние поля)
Moves solveBeam(const Problem& pr, const Options& opt, const Timer& tm);
// best   -- портфель: запускает подходящие алгоритмы и берёт лучший результат
Moves solveBest(const Problem& pr, const Options& opt, const Timer& tm);

// IDA* с начальным решением (рекордом). Возвращает лучшую найденную
// последовательность; optimal = true, если оптимальность доказана.
struct IdaResult {
    Moves moves;
    long long F = 0;
    bool optimal = false;
};
IdaResult idaSearch(const Problem& pr, const Moves& incumbent, double timeLimit, const Options& opt);

// Подзадача: прямоугольник [r0..R-1] x [c0..C-1] (правый нижний угол поля),
// снаружи которого все фишки уже стоят на местах. Ходы подзадачи совпадают
// с ходами исходной задачи (пустая клетка не выходит из прямоугольника).
bool makeSubProblem(const Problem& pr, const Board& b, int r0, int c0, Problem& sub);

}  // namespace p15
