// lns.h -- решатель "послойная сборка + LNS по последовательности ходов".
// Не зависит от astar.h / builder.h; тип хода свой (LnsMove), чтобы не конфликтовать с Move.
#ifndef LNS_H
#define LNS_H

#include <array>
#include <cstdint>
#include <vector>

struct LnsMove {
    uint16_t tile;  // номер фишки
    char dir;       // направление движения фишки: U D L R
};

struct LnsResult {
    std::vector<LnsMove> moves;
    long long F = 0;        // 100 * P - sum(cost)
    long long cost = 0;
    int placed = 0;         // P
    bool unsolvable = false;
};

// rows x cols, weights = цены ходов фишки U D L R, cells = 0 для пустой клетки.
// time_limit -- секунды (общий бюджет), verbose -- журнал в stderr.
LnsResult solve_lns(uint32_t rows, uint32_t cols, const std::array<uint32_t, 4>& weights,
                    const std::vector<uint16_t>& cells, double time_limit, int verbose = 0);

#endif  // LNS_H
