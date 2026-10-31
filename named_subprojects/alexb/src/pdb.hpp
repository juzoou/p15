// pdb.hpp -- аддитивные базы данных образцов (pattern databases) с весами ходов.
//
// Для группы фишек хранится минимальная стоимость ходов ЭТИХ фишек, нужная,
// чтобы поставить их на целевые клетки (ходы прочих фишек бесплатны).
// Суммы по непересекающимся группам -- допустимая оценка снизу.
#pragma once
#include "common.hpp"

namespace p15 {

class PatternDB {
public:
    // tiles -- номера фишек группы, goal -- их целевые клетки.
    // false, если пространство состояний больше maxStates или стоимости слишком велики.
    bool build(int R, int C, const std::vector<int>& tiles, const std::vector<int>& goal,
               const long long cost[4], size_t maxStates);
    // индекс расположения: cells[i] -- клетка i-й фишки группы
    uint64_t index(const int* cells) const {
        uint64_t idx = 0;
        for (int i = 0; i < k; i++) {
            int r = cells[i];
            for (int j = 0; j < i; j++)
                if (cells[j] < cells[i]) r--;
            idx += (uint64_t)r * mult[i];
        }
        return idx;
    }
    uint32_t value(uint64_t idx) const { return table[idx]; }

    int k = 0, N = 0;
    std::vector<int> tiles, goal;

private:
    int R = 0, C = 0;
    std::vector<uint64_t> mult;
    std::vector<uint32_t> table;
    void unrank(uint64_t idx, int* cells) const;
};

// разбиение фишек 1..R*C-1 на компактные группы размера <= k
std::vector<std::vector<int>> makePartition(int R, int C, int k);

}  // namespace p15
