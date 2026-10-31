#include "pyatnashki/types.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace pyatnashki {

// Column-by-column is the transposed constructive strategy. Tile labels are
// remapped to make the transpose's target canonical, then mapped back afterward.
SearchResult column_by_column(const Puzzle& puzzle) {
    const auto started = std::chrono::steady_clock::now();
    const int rows = puzzle.rows;
    const int cols = puzzle.cols;
    const int cells = rows * cols;

    Puzzle transposed;
    transposed.rows = cols;
    transposed.cols = rows;
    // In the transposed board: U<->original L, D<->original R,
    // L<->original U, R<->original D.
    transposed.costs = {puzzle.costs[2], puzzle.costs[3], puzzle.costs[0], puzzle.costs[1]};
    transposed.has_cost_block = puzzle.has_cost_block;
    transposed.has_order = false;

    std::vector<int> old_to_new(static_cast<std::size_t>(cells), 0);
    std::vector<int> new_to_old(static_cast<std::size_t>(cells), 0);
    for (int old_tile = 1; old_tile < cells; ++old_tile) {
        const int old_goal_index = old_tile - 1;
        const int old_goal_row = old_goal_index / cols;
        const int old_goal_col = old_goal_index % cols;
        const int new_goal_index = old_goal_col * rows + old_goal_row;
        const int new_tile = new_goal_index + 1;
        old_to_new[static_cast<std::size_t>(old_tile)] = new_tile;
        new_to_old[static_cast<std::size_t>(new_tile)] = old_tile;
    }

    transposed.start.assign(static_cast<std::size_t>(cells), 0);
    for (int old_index = 0; old_index < cells; ++old_index) {
        const int old_row = old_index / cols;
        const int old_col = old_index % cols;
        const int new_index = old_col * rows + old_row;
        const int old_tile = puzzle.start[static_cast<std::size_t>(old_index)];
        transposed.start[static_cast<std::size_t>(new_index)] =
            old_tile == 0 ? 0 : old_to_new[static_cast<std::size_t>(old_tile)];
    }

    SearchResult result = row_by_row(transposed);
    for (Move& move : result.moves) {
        if (move.tile <= 0 || move.tile >= cells)
            throw std::runtime_error("column-by-column solver: invalid transposed tile id");
        move.tile = new_to_old[static_cast<std::size_t>(move.tile)];
        switch (move.direction) {
        case 'U': move.direction = 'L'; break;
        case 'D': move.direction = 'R'; break;
        case 'L': move.direction = 'U'; break;
        case 'R': move.direction = 'D'; break;
        default: throw std::runtime_error("column-by-column solver: invalid direction");
        }
    }

    // Recompute using original U/D/L/R costs to catch any mapping regression.
    Cost verified_cost = 0;
    for (const Move& move : result.moves) {
        const std::size_t index = move.direction == 'U' ? 0 : move.direction == 'D' ? 1 :
                                  move.direction == 'L' ? 2 : 3;
        if (verified_cost > std::numeric_limits<Cost>::max() - puzzle.costs[index])
            throw std::runtime_error("column-by-column solver: total cost overflow");
        verified_cost += puzzle.costs[index];
    }
    result.total_cost = verified_cost;
    result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return result;
}

} // namespace pyatnashki
