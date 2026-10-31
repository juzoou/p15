#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pyatnashki {

using Cost = std::int64_t;
using Board = std::vector<int>;

struct BoardHash {
    std::size_t operator()(const Board& board) const noexcept {
        std::size_t hash = 1469598103934665603ULL;
        for (int value : board) {
            hash ^= static_cast<std::size_t>(value);
            hash *= 1099511628211ULL;
        }
        return hash;
    }
};

struct Move {
    int tile = 0;
    char direction = ' ';
};

struct Puzzle {
    int rows = 0;
    int cols = 0;
    Board start;
    std::array<Cost, 4> costs{1, 1, 1, 1}; // U, D, L, R (tile movement direction)
    bool has_cost_block = false;
    std::vector<Move> order;
    bool has_order = false;

    Board goal() const {
        Board result(start.size());
        for (std::size_t i = 0; i + 1 < result.size(); ++i) result[i] = static_cast<int>(i) + 1;
        if (!result.empty()) result.back() = 0;
        return result;
    }
};

struct SearchResult {
    std::vector<Move> moves;
    Cost total_cost = 0;
    std::uint64_t expanded = 0;
    double seconds = 0;
};

struct CheckResult {
    Board final_board;
    std::size_t placed = 0;
    Cost total_cost = 0;
};

int direction_index(char direction);
Cost score_for(Cost tile_count, Cost total_cost);
std::string upper(std::string value);
long long parse_integer(const std::string& token, const std::string& what);
Puzzle parse_puzzle(const std::string& path);
bool solvable(const Puzzle& puzzle);
CheckResult check_order(const Puzzle& puzzle);
std::string format_board(const Board& board, int rows, int cols);
std::string render_output(const Puzzle& puzzle, const std::vector<Move>& moves,
                          const std::string& summary);
void write_text(const std::string& path, const std::string& text);

// Exact (cost-optimal, when they terminate) searches.
SearchResult best_first(const Puzzle& puzzle, std::uint64_t max_expanded, bool use_heuristic);
SearchResult weighted_a_star(const Puzzle& puzzle, std::uint64_t max_expanded, double weight);
SearchResult linear_conflict_astar(const Puzzle& puzzle, std::uint64_t max_expanded);
SearchResult rbfs(const Puzzle& puzzle, std::uint64_t max_expanded);
SearchResult pattern_database(const Puzzle& puzzle, std::uint64_t max_expanded);
SearchResult simulated_annealing(const Puzzle& puzzle, std::uint64_t iteration_budget);
SearchResult ida_star(const Puzzle& puzzle, std::uint64_t max_expanded);
SearchResult zero_one_bfs(const Puzzle& puzzle, std::uint64_t max_expanded);
SearchResult dial(const Puzzle& puzzle, std::uint64_t max_expanded);
SearchResult bidirectional_dijkstra(const Puzzle& puzzle, std::uint64_t max_expanded);

// Constructive solvers for large boards. Neither promises minimum cost.
SearchResult row_by_row(const Puzzle& puzzle);
SearchResult column_by_column(const Puzzle& puzzle);

} // namespace pyatnashki
