// stages.hpp -- общие для reduce и beam операции послойной сборки.
#pragma once
#include "route.hpp"

namespace p15 {

// клетки строки r0 (isRow) или столбца c0 текущей области в порядке заполнения
std::vector<int> lineCells(const Problem& pr, int r0, int c0, bool isRow, bool rev);
// точный поиск в окне W: поставить фишку ta в x1 и tb в x2
bool windowPair(const Problem& pr, Board& b, int ta, int tb, int x1, int x2,
                const std::vector<int>& W, Moves& out);
// две последние клетки линии L (x1 = L[k-2], x2 = L[k-1]); in -- направление внутрь области
bool placePair(const Problem& pr, Board& b, Router& rt, const std::vector<int>& L, int in, int depth,
               Moves& out);
// заполнить всю линию: по одной фишке, затем пара
bool fillLine(const Problem& pr, Board& b, Router& rt, const std::vector<int>& L, int in, int depth,
              Moves& out);
// применить ходы к полю и дописать в out (false -- ход невозможен)
bool applyMoves(Board& b, const Moves& mv, Moves& out);

}  // namespace p15
