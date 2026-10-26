// route.hpp -- проводка одной фишки / пустой клетки по полю с запретными клетками.
#pragma once
#include "common.hpp"

namespace p15 {

class Router {
public:
    explicit Router(const Problem& pr);

    std::vector<uint8_t> locked;   // 1 = клетку трогать нельзя (фишка уже на месте)

    // Перевести пустую клетку в goal, не задевая locked и avoid (-1 = нет).
    // Ходы дописываются в out и применяются к b. false -- пути нет.
    bool moveBlank(Board& b, int goal, int avoid, Moves& out);
    // То же, но в ближайшую (по стоимости) из нескольких клеток.
    bool moveBlankAny(Board& b, const std::vector<int>& goals, int avoid, Moves& out);
    // Перевести фишку t в клетку goal оптимальным (по стоимости) способом
    // в модели "фишка + пустая клетка". Ходы дописываются в out и применяются к b.
    bool moveTile(Board& b, int t, int goal, Moves& out);
    // Только стоимость такой проводки (поле не меняется); -1 -- невозможно
    // или дороже limit (limit < 0 -- без ограничения).
    long long tileCost(const Board& b, int t, int goal, long long limit = -1);

    long long lastCost = 0;        // стоимость последней успешной проводки

private:
    const Problem& pr;
    int R, C, N;
    long long cmin;
    uint32_t gen = 1;

    int nbr(int cell, int d) const {
        int r = cell / C + DROW[d], c = cell % C + DCOL[d];
        if (r < 0 || r >= R || c < 0 || c >= C) return -1;
        return r * C + c;
    }
    long long hTile(int p, int goal) const;

    // Дейкстра для пустой клетки по всему полю
    std::vector<long long> gd;
    std::vector<int> gpar, gtouched;
    int blankDijkstra(int src, const int* targets, int nt, int avoid, long long* outCost);
    void extractBlankPath(int src, int dst, Moves& path) const;

    // обход вокруг фишки: из клетки со стороны s1 в клетку со стороны s2
    std::vector<uint32_t> aroundGen;
    std::vector<long long> aroundVal;
    long long aroundCost(int p, int s1, int s2, Moves* path);

    // A* по состояниям (клетка фишки, сторона пустой клетки)
    std::vector<long long> sd;
    std::vector<int> spar, stouched;
    long long search(const Board& b, int t, int goal, long long limit, std::vector<int>* chain);
};

}  // namespace p15
