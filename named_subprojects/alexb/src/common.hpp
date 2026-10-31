// common.hpp -- общие определения для всех решателей "Пятнашек".
//
// Соглашения:
//  * поле R строк x C столбцов, клетки нумеруются построчно: cell = r*C + c;
//  * a[cell] = номер фишки, 0 = свободная клетка;
//  * фишка t должна стоять в клетке t-1, свободная клетка -- в N-1 (правый нижний угол);
//  * внутри решателей ход описывается направлением движения ПУСТОЙ клетки (bd);
//    при ходе пустой клетки в направлении bd фишка движется в направлении bd^1;
//  * стоимости cost[] заданы по направлению движения ФИШКИ в порядке U D L R.
#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace p15 {

// алгоритм не применим к данной задаче (например, поле слишком велико)
struct NotApplicable : std::runtime_error {
    using std::runtime_error::runtime_error;
};

constexpr long long TILE_VALUE = 100;   // "100" из формулы F = 100*P - sum(Cd*Nd)

enum Dir : int { UP = 0, DOWN = 1, LEFT = 2, RIGHT = 3 };
extern const int DROW[4];
extern const int DCOL[4];
extern const char DIRCHAR[4];
inline int opposite(int d) { return d ^ 1; }

using Moves = std::vector<uint8_t>;     // последовательность ходов пустой клетки

struct Problem {
    int R = 0, C = 0;                   // строки, столбцы
    std::vector<int> start;             // R*C клеток построчно, 0 = пусто
    long long cost[4] = {1, 1, 1, 1};   // стоимость хода фишки U D L R
    bool costGiven = false;
    bool hasOrder = false;              // режим проверки: задан блок ORDER
    std::vector<std::pair<int, int>> order;  // (фишка, направление фишки)
    std::vector<int> orderLine;         // номер строки файла для каждого хода
    std::vector<std::string> warnings;
    std::string sourceFormat;           // "DATA" или "simple"

    int cells() const { return R * C; }
    int tiles() const { return R * C - 1; }
    // стоимость хода пустой клетки в направлении bd (= ход фишки в bd^1)
    long long blankCost(int bd) const { return cost[bd ^ 1]; }
    bool allCostsZero() const { return cost[0] == 0 && cost[1] == 0 && cost[2] == 0 && cost[3] == 0; }
};

// ---------- разбор входных файлов ----------
// Поддерживаются оба формата: формат презентации (DATA/COST/ORDER) и
// упрощённый формат из плана (cU cD cL cR w h a1 ... a_{w*h}, 0 = пусто).
bool parseText(const std::string& text, Problem& pr, std::string& err, bool strict);
bool parseFile(const std::string& path, Problem& pr, std::string& err, bool strict);

// ---------- состояние поля ----------
struct Board {
    int R = 0, C = 0, N = 0;
    std::vector<int> a;     // клетка -> фишка
    std::vector<int> pos;   // фишка -> клетка (pos[0] = пустая клетка)
    int blank = -1;

    Board() = default;
    explicit Board(const Problem& pr) : Board(pr.R, pr.C, pr.start) {}
    Board(int R_, int C_, const std::vector<int>& cells);

    int row(int cell) const { return cell / C; }
    int col(int cell) const { return cell % C; }
    int nb(int cell, int d) const {      // сосед клетки в направлении d или -1
        int r = cell / C + DROW[d], c = cell % C + DCOL[d];
        if (r < 0 || r >= R || c < 0 || c >= C) return -1;
        return r * C + c;
    }
    bool canMove(int bd) const { return nb(blank, bd) >= 0; }
    int move(int bd) {                   // ход пустой клетки, возвращает сдвинутую фишку
        int n = nb(blank, bd);
        int t = a[n];
        a[blank] = t; pos[t] = blank;
        a[n] = 0; pos[0] = n;
        blank = n;
        return t;
    }
    bool inPlace(int t) const { return pos[t] == t - 1; }
    int placed() const;
};

// direction from cell u to the adjacent cell v
int dirBetween(int C, int u, int v);

// ---------- достижимость ----------
// true, если конечное расположение достижимо из начального
bool goalReachable(int R, int C, const std::vector<int>& cells);
inline bool goalReachable(const Problem& pr) { return goalReachable(pr.R, pr.C, pr.start); }
// чётность "класса" позиции: чётность перестановки XOR чётность расстояния пустой до угла
int parityClass(int R, int C, const std::vector<int>& cells);

// ---------- оценка решения ----------
struct Score {
    long long F = 0, cost = 0;
    int P = 0;
    size_t moves = 0;
    long long cnt[4] = {0, 0, 0, 0};   // число ходов фишек по направлениям U D L R
};
bool simulate(const Problem& pr, const Moves& mv, Score& sc, Board* fin = nullptr);
Score scoreOfEmpty(const Problem& pr);
// обрезать последовательность до лучшего (по F) префикса
Moves bestPrefix(const Problem& pr, const Moves& mv);
// убрать циклы (повторения позиций) и обрезать до лучшего префикса
Moves polish(const Problem& pr, const Moves& mv);

// ---------- время ----------
struct Timer {
    std::chrono::steady_clock::time_point t0;
    double limit;   // секунды, <= 0 -- без ограничения
    explicit Timer(double lim = 0) : t0(std::chrono::steady_clock::now()), limit(lim) {}
    double elapsed() const {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    }
    bool expired() const { return limit > 0 && elapsed() >= limit; }
    double remaining() const { return limit > 0 ? limit - elapsed() : 1e18; }
};

// ---------- параметры командной строки ----------
struct Options {
    double timeLimit = 0;
    uint64_t seed = 12345;
    int verbose = 0;
    bool strict = false;
    std::map<std::string, std::string> params;
    double getD(const std::string& k, double def) const;
    long long getI(const std::string& k, long long def) const;
    bool has(const std::string& k) const { return params.count(k) != 0; }
};

// Решатель: по задаче возвращает последовательность ходов пустой клетки.
// Может бросить std::runtime_error (например, "board too large").
using SolverFn = std::function<Moves(const Problem&, const Options&, const Timer&)>;

// Общая точка входа: argv = <задача> <файл решения> [--time T] [--seed S] [-v] ...
// Печатает в stdout значение F, пишет файл решения в формате DATA/COST/ORDER.
int runSolverMain(int argc, char** argv, const std::string& algoName,
                  double defaultTime, SolverFn fn);

// решатель для полей 1xM и Nx1 (там всё определяется положением пустой клетки)
Moves solveLine(const Problem& pr);

// простой ГПСЧ (одинаковый на всех платформах)
struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed = 1) : s(seed * 0x9E3779B97F4A7C15ULL + 0x1234567ULL) {}
    uint64_t next() {
        s += 0x9E3779B97F4A7C15ULL;
        uint64_t z = s;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    int below(int n) { return (int)(next() % (uint64_t)n); }
    double uni() { return (next() >> 11) * (1.0 / 9007199254740992.0); }
};

inline uint64_t mix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}
// ключ Зобриста для пары (клетка, фишка), вычисляется на лету
inline uint64_t zkey(int cell, int tile) {
    return mix64(((uint64_t)(uint32_t)cell << 32) | (uint32_t)tile);
}

void logf(int level, const Options& opt, const char* fmt, ...);

}  // namespace p15
