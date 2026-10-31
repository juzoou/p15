#include "pyatnashki/types.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <queue>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pyatnashki {
namespace { constexpr Cost INF = std::numeric_limits<Cost>::max(); }

// A scalable constructive solver. It does not guarantee globally minimum cost,
// but solves every reachable 2-D rectangular sliding puzzle in polynomial work.
struct GridWorkspace {
    std::vector<int> seen;
    std::vector<int> parent;
    std::vector<int> queue;
    int epoch = 0;
    std::uint64_t visited_total = 0;

    explicit GridWorkspace(std::size_t size) : seen(size, 0), parent(size, -1) {
        queue.reserve(size);
    }

    int next_epoch() {
        if (epoch == std::numeric_limits<int>::max()) {
            std::fill(seen.begin(), seen.end(), 0);
            epoch = 0;
        }
        return ++epoch;
    }

    template <class Allowed>
    std::vector<int> find_path(int start, int goal, int rows, int cols,
                               Allowed allowed, bool local = false,
                               int center = -1) {
        if (start == goal) return {start};
        if (start < 0 || goal < 0 || static_cast<std::size_t>(start) >= seen.size() ||
            static_cast<std::size_t>(goal) >= seen.size()) return {};
        if (!allowed(start) || !allowed(goal)) return {};

        const int my_epoch = next_epoch();
        queue.clear();
        queue.push_back(start);
        seen[static_cast<std::size_t>(start)] = my_epoch;
        parent[static_cast<std::size_t>(start)] = -1;

        const int center_row = center >= 0 ? center / cols : -1;
        const int center_col = center >= 0 ? center % cols : -1;
        std::size_t head = 0;
        while (head < queue.size()) {
            const int current = queue[head++];
            ++visited_total;
            if (current == goal) break;
            const int row = current / cols;
            const int col = current % cols;
            const std::array<std::pair<int, int>, 4> deltas{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
            for (const auto& delta : deltas) {
                const int nr = row + delta.first;
                const int nc = col + delta.second;
                if (nr < 0 || nr >= rows || nc < 0 || nc >= cols) continue;
                if (local && (std::abs(nr - center_row) > 1 || std::abs(nc - center_col) > 1)) continue;
                const int next = nr * cols + nc;
                if (seen[static_cast<std::size_t>(next)] == my_epoch || !allowed(next)) continue;
                seen[static_cast<std::size_t>(next)] = my_epoch;
                parent[static_cast<std::size_t>(next)] = current;
                queue.push_back(next);
            }
        }
        if (seen[static_cast<std::size_t>(goal)] != my_epoch) return {};
        std::vector<int> path;
        for (int at = goal; at != -1; at = parent[static_cast<std::size_t>(at)]) path.push_back(at);
        std::reverse(path.begin(), path.end());
        return path;
    }
};

void append_move(Board& board, int& blank, int next, const Puzzle& puzzle,
                 std::vector<Move>& moves, Cost& total_cost) {
    const int blank_row = blank / puzzle.cols;
    const int blank_col = blank % puzzle.cols;
    const int next_row = next / puzzle.cols;
    const int next_col = next % puzzle.cols;
    char direction = ' ';
    if (next_row == blank_row + 1 && next_col == blank_col) direction = 'U';
    else if (next_row == blank_row - 1 && next_col == blank_col) direction = 'D';
    else if (next_row == blank_row && next_col == blank_col + 1) direction = 'L';
    else if (next_row == blank_row && next_col == blank_col - 1) direction = 'R';
    else throw std::runtime_error("internal row-by-row error: non-adjacent blank move");

    const Cost step_cost = puzzle.costs[static_cast<std::size_t>(direction_index(direction))];
    if (total_cost > INF - step_cost) throw std::runtime_error("path cost overflow");
    const int tile = board[static_cast<std::size_t>(next)];
    if (tile == 0) throw std::runtime_error("internal row-by-row error: blank moved into blank");
    moves.push_back({tile, direction});
    total_cost += step_cost;
    std::swap(board[static_cast<std::size_t>(blank)], board[static_cast<std::size_t>(next)]);
    blank = next;
}

std::vector<int> active_path(GridWorkspace& workspace, int start, int goal,
                             const Puzzle& puzzle, const std::vector<bool>& locked,
                             const std::vector<bool>& protected_cells,
                             int forbidden = -1, bool local = false, int center = -1) {
    auto allowed = [&](int cell) {
        return cell != forbidden && !locked[static_cast<std::size_t>(cell)] &&
               !protected_cells[static_cast<std::size_t>(cell)];
    };
    return workspace.find_path(start, goal, puzzle.rows, puzzle.cols, allowed, local, center);
}

void route_blank_to(Board& board, int& blank, int destination, int forbidden_tile_pos,
                    const Puzzle& puzzle, const std::vector<bool>& locked,
                    const std::vector<bool>& protected_cells, GridWorkspace& workspace,
                    std::vector<Move>& moves, Cost& total_cost, bool allow_local = true) {
    if (blank == destination) return;
    std::vector<int> path;
    if (allow_local && forbidden_tile_pos >= 0) {
        const int tr = forbidden_tile_pos / puzzle.cols;
        const int tc = forbidden_tile_pos % puzzle.cols;
        const int br = blank / puzzle.cols, bc = blank % puzzle.cols;
        const int dr = destination / puzzle.cols, dc = destination % puzzle.cols;
        if (std::abs(br - tr) <= 1 && std::abs(bc - tc) <= 1 &&
            std::abs(dr - tr) <= 1 && std::abs(dc - tc) <= 1) {
            path = active_path(workspace, blank, destination, puzzle, locked,
                               protected_cells, forbidden_tile_pos, true,
                               forbidden_tile_pos);
        }
    }
    if (path.empty()) {
        path = active_path(workspace, blank, destination, puzzle, locked, protected_cells,
                           forbidden_tile_pos, false, forbidden_tile_pos);
    }
    if (path.empty()) {
        throw std::runtime_error("row-by-row solver: blank cannot reach staging cell " +
                                 std::to_string(destination) +
                                 " without moving a protected tile");
    }
    for (std::size_t i = 1; i < path.size(); ++i) {
        append_move(board, blank, path[i], puzzle, moves, total_cost);
    }
}

void place_tile(Board& board, int& blank, int tile, int target,
                const Puzzle& puzzle, const std::vector<bool>& locked,
                const std::vector<bool>& protected_cells, GridWorkspace& workspace,
                std::vector<Move>& moves, Cost& total_cost) {
    auto found = std::find(board.begin(), board.end(), tile);
    if (found == board.end()) throw std::runtime_error("row-by-row solver: target tile is missing");
    int tile_pos = static_cast<int>(found - board.begin());
    if (locked[static_cast<std::size_t>(target)] || protected_cells[static_cast<std::size_t>(target)]) {
        throw std::runtime_error("row-by-row solver: target cell is already protected");
    }
    auto path = active_path(workspace, tile_pos, target, puzzle, locked, protected_cells);
    if (path.empty()) throw std::runtime_error("row-by-row solver: target tile has no path to its staging cell");

    for (std::size_t i = 1; i < path.size(); ++i) {
        const int next_tile_pos = path[i];
        route_blank_to(board, blank, next_tile_pos, tile_pos, puzzle, locked,
                       protected_cells, workspace, moves, total_cost, true);
        // The blank is now at the next tile position. Sliding it into the current
        // tile position moves the target tile exactly one step along the planned path.
        append_move(board, blank, tile_pos, puzzle, moves, total_cost);
        tile_pos = next_tile_pos;
    }
    if (tile_pos != target || board[static_cast<std::size_t>(target)] != tile) {
        throw std::runtime_error("row-by-row solver: failed to place a target tile");
    }
}

std::vector<int> abstract_patch_path(int patch_rows, int patch_cols,
                                     int start_a, int start_b, int start_blank,
                                     int goal_a, int goal_b) {
    const int cells = patch_rows * patch_cols;
    const int state_count = cells * cells * cells;
    auto encode = [cells](int a, int b, int blank) { return (a * cells + b) * cells + blank; };
    std::vector<int> parent(static_cast<std::size_t>(state_count), -2);
    std::vector<int> queue;
    queue.reserve(static_cast<std::size_t>(state_count));
    const int start = encode(start_a, start_b, start_blank);
    parent[static_cast<std::size_t>(start)] = -1;
    queue.push_back(start);
    int goal_state = -1;
    std::size_t head = 0;
    while (head < queue.size()) {
        const int state = queue[head++];
        const int blank = state % cells;
        const int b = (state / cells) % cells;
        const int a = state / (cells * cells);
        if (a == goal_a && b == goal_b) { goal_state = state; break; }
        const int row = blank / patch_cols, col = blank % patch_cols;
        const std::array<std::pair<int, int>, 4> deltas{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
        for (const auto& delta : deltas) {
            const int nr = row + delta.first, nc = col + delta.second;
            if (nr < 0 || nr >= patch_rows || nc < 0 || nc >= patch_cols) continue;
            const int next_blank = nr * patch_cols + nc;
            int next_a = a, next_b = b;
            if (next_blank == a) next_a = blank;
            if (next_blank == b) next_b = blank;
            const int next_state = encode(next_a, next_b, next_blank);
            if (parent[static_cast<std::size_t>(next_state)] != -2) continue;
            parent[static_cast<std::size_t>(next_state)] = state;
            queue.push_back(next_state);
        }
    }
    if (goal_state < 0) throw std::runtime_error("row-by-row solver: internal pair macro is unreachable");
    std::vector<int> blank_path;
    for (int state = goal_state; state != start; state = parent[static_cast<std::size_t>(state)]) {
        blank_path.push_back(state % cells);
    }
    std::reverse(blank_path.begin(), blank_path.end());
    return blank_path;
}

void solve_pair_patch(Board& board, int& blank, const Puzzle& puzzle,
                      int origin_row, int origin_col, int patch_rows, int patch_cols,
                      int tile_a, int tile_b, int goal_a, int goal_b,
                      std::vector<Move>& moves, Cost& total_cost) {
    auto local_position = [&](int global) {
        const int row = global / puzzle.cols - origin_row;
        const int col = global % puzzle.cols - origin_col;
        if (row < 0 || row >= patch_rows || col < 0 || col >= patch_cols)
            throw std::runtime_error("row-by-row solver: tile escaped its local pair patch");
        return row * patch_cols + col;
    };
    auto find_global = [&](int tile) {
        auto found = std::find(board.begin(), board.end(), tile);
        if (found == board.end()) throw std::runtime_error("row-by-row solver: pair tile disappeared");
        return static_cast<int>(found - board.begin());
    };
    const int local_a = local_position(find_global(tile_a));
    const int local_b = local_position(find_global(tile_b));
    const int local_blank = local_position(blank);
    const auto path = abstract_patch_path(patch_rows, patch_cols, local_a, local_b,
                                          local_blank, goal_a, goal_b);
    for (int local_next : path) {
        const int next_row = origin_row + local_next / patch_cols;
        const int next_col = origin_col + local_next % patch_cols;
        append_move(board, blank, next_row * puzzle.cols + next_col, puzzle, moves, total_cost);
    }
    if (local_position(find_global(tile_a)) != goal_a || local_position(find_global(tile_b)) != goal_b) {
        throw std::runtime_error("row-by-row solver: pair macro did not place both target tiles");
    }
}

// Exact abstraction for the thin two-row phase: the only identities that matter
// are one target tile and the blank. At most 200 active cells remain here, so the
// O(V^2) state graph is tiny compared with the full puzzle state space.
void place_tile_and_blank_in_patch(Board& board, int& blank, int tile, int target,
                                   const Puzzle& puzzle, const std::vector<bool>& locked,
                                   const std::vector<bool>& protected_cells,
                                   const std::vector<int>& patch_cells,
                                   std::vector<Move>& moves, Cost& total_cost) {
    const std::size_t n = board.size();
    std::vector<int> allowed_cells;
    std::vector<int> local_index(n, -1);
    for (std::size_t cell = 0; cell < n; ++cell) {
        if (!locked[cell] && !protected_cells[cell]) {
            local_index[cell] = static_cast<int>(allowed_cells.size());
            allowed_cells.push_back(static_cast<int>(cell));
        }
    }
    const int v = static_cast<int>(allowed_cells.size());
    if (v == 0 || v > 10000) throw std::runtime_error("row-by-row solver: invalid thin-board state graph");
    const int tile_global = static_cast<int>(std::find(board.begin(), board.end(), tile) - board.begin());
    if (tile_global < 0 || static_cast<std::size_t>(tile_global) >= n ||
        local_index[static_cast<std::size_t>(tile_global)] < 0 ||
        local_index[static_cast<std::size_t>(blank)] < 0 ||
        local_index[static_cast<std::size_t>(target)] < 0) {
        throw std::runtime_error("row-by-row solver: invalid tile/blank state for thin-board search");
    }
    std::vector<bool> patch_flag(static_cast<std::size_t>(v), false);
    for (int cell : patch_cells) {
        if (cell >= 0 && static_cast<std::size_t>(cell) < n) {
            const int li = local_index[static_cast<std::size_t>(cell)];
            if (li >= 0) patch_flag[static_cast<std::size_t>(li)] = true;
        }
    }
    const int start_tile = local_index[static_cast<std::size_t>(tile_global)];
    const int start_blank = local_index[static_cast<std::size_t>(blank)];
    const int target_local = local_index[static_cast<std::size_t>(target)];
    const std::size_t state_count = static_cast<std::size_t>(v) * static_cast<std::size_t>(v);
    std::vector<int> parent(state_count, -2);
    std::vector<int> moved_to(state_count, -1);
    std::vector<int> queue;
    queue.reserve(state_count);
    const int start_state = start_tile * v + start_blank;
    parent[static_cast<std::size_t>(start_state)] = -1;
    queue.push_back(start_state);
    int goal_state = -1;
    std::size_t head = 0;
    while (head < queue.size()) {
        const int state = queue[head++];
        const int tile_local = state / v;
        const int blank_local = state % v;
        if (tile_local == target_local && patch_flag[static_cast<std::size_t>(blank_local)]) {
            goal_state = state;
            break;
        }
        const int blank_global = allowed_cells[static_cast<std::size_t>(blank_local)];
        const int row = blank_global / puzzle.cols;
        const int col = blank_global % puzzle.cols;
        const std::array<std::pair<int, int>, 4> deltas{{{1,0},{-1,0},{0,1},{0,-1}}};
        for (const auto& delta : deltas) {
            const int nr = row + delta.first, nc = col + delta.second;
            if (nr < 0 || nr >= puzzle.rows || nc < 0 || nc >= puzzle.cols) continue;
            const int next_global = nr * puzzle.cols + nc;
            const int next_local = local_index[static_cast<std::size_t>(next_global)];
            if (next_local < 0) continue;
            int next_tile_local = tile_local;
            int next_blank_local = next_local;
            if (next_local == tile_local) {
                next_tile_local = blank_local;
                next_blank_local = tile_local;
            }
            const int next_state = next_tile_local * v + next_blank_local;
            if (parent[static_cast<std::size_t>(next_state)] != -2) continue;
            parent[static_cast<std::size_t>(next_state)] = state;
            moved_to[static_cast<std::size_t>(next_state)] = next_global;
            queue.push_back(next_state);
        }
    }
    if (goal_state < 0) throw std::runtime_error("row-by-row solver: could not stage a tile in the final two-row strip");
    std::vector<int> steps;
    for (int state = goal_state; state != start_state; state = parent[static_cast<std::size_t>(state)]) {
        steps.push_back(moved_to[static_cast<std::size_t>(state)]);
    }
    std::reverse(steps.begin(), steps.end());
    for (int next_global : steps) append_move(board, blank, next_global, puzzle, moves, total_cost);
}

// Fallback for a two-row pair that cannot use the simple staging macro. Searching
// only (tile A, tile B, blank) states is O(V^3), where V<=200 in this phase.
void solve_pair_in_two_row_strip(Board& board, int& blank, const Puzzle& puzzle,
                                 const std::vector<bool>& locked,
                                 int tile_a, int tile_b, int goal_a, int goal_b,
                                 std::vector<Move>& moves, Cost& total_cost) {
    const std::size_t n = board.size();
    std::vector<int> cells;
    std::vector<int> local_index(n, -1);
    for (std::size_t i = 0; i < n; ++i) {
        if (!locked[i]) {
            local_index[i] = static_cast<int>(cells.size());
            cells.push_back(static_cast<int>(i));
        }
    }
    const int v = static_cast<int>(cells.size());
    if (v < 4 || v > 200) throw std::runtime_error("row-by-row solver: invalid active width in pair fallback");
    auto find_tile = [&](int tile) {
        const auto it = std::find(board.begin(), board.end(), tile);
        if (it == board.end()) throw std::runtime_error("row-by-row solver: pair fallback cannot find a tile");
        return static_cast<int>(it - board.begin());
    };
    const int a_global = find_tile(tile_a), b_global = find_tile(tile_b);
    const int a_start = local_index[static_cast<std::size_t>(a_global)];
    const int b_start = local_index[static_cast<std::size_t>(b_global)];
    const int z_start = local_index[static_cast<std::size_t>(blank)];
    const int a_goal = local_index[static_cast<std::size_t>(goal_a)];
    const int b_goal = local_index[static_cast<std::size_t>(goal_b)];
    if (a_start < 0 || b_start < 0 || z_start < 0 || a_goal < 0 || b_goal < 0)
        throw std::runtime_error("row-by-row solver: invalid state in pair fallback");

    const int state_count = v * v * v;
    auto encode = [v](int a, int b, int z) { return (a * v + b) * v + z; };
    std::vector<int> parent(static_cast<std::size_t>(state_count), -2);
    std::vector<int> queue;
    queue.reserve(static_cast<std::size_t>(state_count));
    const int start = encode(a_start, b_start, z_start);
    parent[static_cast<std::size_t>(start)] = -1;
    queue.push_back(start);
    int finish = -1;
    std::size_t head = 0;
    while (head < queue.size()) {
        const int state = queue[head++];
        const int z = state % v;
        const int b = (state / v) % v;
        const int a = state / (v * v);
        if (a == a_goal && b == b_goal) { finish = state; break; }
        const int global_z = cells[static_cast<std::size_t>(z)];
        const int row = global_z / puzzle.cols, col = global_z % puzzle.cols;
        for (const auto& delta : std::array<std::pair<int, int>, 4>{{{1,0},{-1,0},{0,1},{0,-1}}}) {
            const int nr = row + delta.first, nc = col + delta.second;
            if (nr < 0 || nr >= puzzle.rows || nc < 0 || nc >= puzzle.cols) continue;
            const int next_global = nr * puzzle.cols + nc;
            const int next_local = local_index[static_cast<std::size_t>(next_global)];
            if (next_local < 0) continue;
            int next_a = a, next_b = b, next_z = next_local;
            if (next_local == a) { next_a = z; next_z = a; }
            else if (next_local == b) { next_b = z; next_z = b; }
            const int next_state = encode(next_a, next_b, next_z);
            if (parent[static_cast<std::size_t>(next_state)] != -2) continue;
            parent[static_cast<std::size_t>(next_state)] = state;
            queue.push_back(next_state);
        }
    }
    if (finish < 0) throw std::runtime_error("row-by-row solver: no pair solution in the remaining two-row strip");
    std::vector<std::pair<int, int>> transitions;
    for (int state = finish; state != start; state = parent[static_cast<std::size_t>(state)]) {
        transitions.emplace_back(parent[static_cast<std::size_t>(state)], state);
    }
    std::reverse(transitions.begin(), transitions.end());
    for (const auto& transition : transitions) {
        const int prev = transition.first, next = transition.second;
        const int prev_a = prev / (v * v), prev_b = (prev / v) % v;
        const int next_a = next / (v * v), next_b = (next / v) % v;
        int destination_local;
        if (next_a != prev_a) destination_local = prev_a;
        else if (next_b != prev_b) destination_local = prev_b;
        else destination_local = next % v;
        append_move(board, blank, cells[static_cast<std::size_t>(destination_local)],
                    puzzle, moves, total_cost);
    }
}

void solve_final_two_by_two(Board& board, int& blank, const Puzzle& puzzle,
                            int origin_row, int origin_col,
                            std::vector<Move>& moves, Cost& total_cost) {
    const std::array<int, 4> cells{{origin_row * puzzle.cols + origin_col,
                                    origin_row * puzzle.cols + origin_col + 1,
                                    (origin_row + 1) * puzzle.cols + origin_col,
                                    (origin_row + 1) * puzzle.cols + origin_col + 1}};
    Board start_state;
    Board goal_state;
    start_state.reserve(4); goal_state.reserve(4);
    int local_blank = -1;
    for (int i = 0; i < 4; ++i) {
        start_state.push_back(board[static_cast<std::size_t>(cells[static_cast<std::size_t>(i)])]);
        goal_state.push_back(cells[static_cast<std::size_t>(i)] + 1 == static_cast<int>(board.size()) ?
                             0 : cells[static_cast<std::size_t>(i)] + 1);
        if (start_state.back() == 0) local_blank = i;
    }
    struct FinalNode { Board state; int blank; int parent; int moved_to; };
    std::vector<FinalNode> nodes;
    nodes.push_back({start_state, local_blank, -1, -1});
    std::unordered_map<Board, int, BoardHash> seen;
    seen.emplace(start_state, 0);
    std::queue<int> queue;
    queue.push(0);
    int goal_id = -1;
    while (!queue.empty()) {
        const int id = queue.front(); queue.pop();
        if (nodes[static_cast<std::size_t>(id)].state == goal_state) { goal_id = id; break; }
        const int z = nodes[static_cast<std::size_t>(id)].blank;
        const int zr = z / 2, zc = z % 2;
        for (const auto& delta : std::array<std::pair<int, int>, 4>{{{1,0},{-1,0},{0,1},{0,-1}}}) {
            const int nr = zr + delta.first, nc = zc + delta.second;
            if (nr < 0 || nr >= 2 || nc < 0 || nc >= 2) continue;
            const int nz = nr * 2 + nc;
            Board next_state = nodes[static_cast<std::size_t>(id)].state;
            std::swap(next_state[static_cast<std::size_t>(z)], next_state[static_cast<std::size_t>(nz)]);
            if (seen.find(next_state) != seen.end()) continue;
            const int next_id = static_cast<int>(nodes.size());
            seen.emplace(next_state, next_id);
            nodes.push_back({std::move(next_state), nz, id, nz});
            queue.push(next_id);
        }
    }
    if (goal_id < 0) throw std::runtime_error("row-by-row solver: final 2x2 state is unreachable");
    std::vector<int> steps;
    for (int id = goal_id; nodes[static_cast<std::size_t>(id)].parent != -1;
         id = nodes[static_cast<std::size_t>(id)].parent) {
        steps.push_back(nodes[static_cast<std::size_t>(id)].moved_to);
    }
    std::reverse(steps.begin(), steps.end());
    for (int local_next : steps) {
        append_move(board, blank, cells[static_cast<std::size_t>(local_next)], puzzle, moves, total_cost);
    }
}

SearchResult row_by_row(const Puzzle& puzzle) {
    const auto started = std::chrono::steady_clock::now();
    const Board goal = puzzle.goal();
    if (puzzle.start == goal) return {{}, 0, 0, 0};
    if (puzzle.rows == 1 || puzzle.cols == 1) {
        if (!solvable(puzzle)) throw std::runtime_error("one-dimensional tile order is unreachable");
        Board board = puzzle.start;
        int blank = static_cast<int>(std::find(board.begin(), board.end(), 0) - board.begin());
        std::vector<Move> moves;
        Cost total_cost = 0;
        while (blank + 1 < static_cast<int>(board.size()))
            append_move(board, blank, blank + 1, puzzle, moves, total_cost);
        return {std::move(moves), total_cost, 0,
                std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count()};
    }

    Board board = puzzle.start;
    int blank = static_cast<int>(std::find(board.begin(), board.end(), 0) - board.begin());
    std::vector<bool> locked(board.size(), false);
    std::vector<bool> protected_cells(board.size(), false);
    std::vector<Move> moves;
    moves.reserve(board.size() * 12);
    Cost total_cost = 0;
    GridWorkspace workspace(board.size());

    // Reduce the board from the top. Each row's last two tiles are placed as a pair.
    for (int row = 0; row < puzzle.rows - 2; ++row) {
        for (int col = 0; col < puzzle.cols - 2; ++col) {
            const int target = row * puzzle.cols + col;
            place_tile(board, blank, target + 1, target, puzzle, locked, protected_cells,
                       workspace, moves, total_cost);
            locked[static_cast<std::size_t>(target)] = true;
        }

        const int goal_a = row * puzzle.cols + puzzle.cols - 2;
        const int goal_b = goal_a + 1;
        const int tile_a = goal_a + 1;
        const int tile_b = goal_b + 1;
        const int stage_a = (row + 1) * puzzle.cols + puzzle.cols - 2;
        const int stage_b = (row + 2) * puzzle.cols + puzzle.cols - 1;
        const Board board_before_pair = board;
        const int blank_before_pair = blank;
        const std::size_t moves_before_pair = moves.size();
        const Cost cost_before_pair = total_cost;
        auto run_top_pair_macro = [&](bool stage_a_first) {
            if (stage_a_first) {
                place_tile(board, blank, tile_a, stage_a, puzzle, locked, protected_cells,
                           workspace, moves, total_cost);
                protected_cells[static_cast<std::size_t>(stage_a)] = true;
                place_tile(board, blank, tile_b, stage_b, puzzle, locked, protected_cells,
                           workspace, moves, total_cost);
                protected_cells[static_cast<std::size_t>(stage_b)] = true;
            } else {
                place_tile(board, blank, tile_b, stage_b, puzzle, locked, protected_cells,
                           workspace, moves, total_cost);
                protected_cells[static_cast<std::size_t>(stage_b)] = true;
                place_tile(board, blank, tile_a, stage_a, puzzle, locked, protected_cells,
                           workspace, moves, total_cost);
                protected_cells[static_cast<std::size_t>(stage_a)] = true;
            }
            // A tile can enter its staging cell from the left in a wide board,
            // leaving the blank just outside the local 3x2 macro. Freeze both
            // target tiles briefly and move the blank into the lower-left patch cell.
            const int pair_blank_cell = (row + 2) * puzzle.cols + puzzle.cols - 2;
            const int blank_row = blank / puzzle.cols, blank_col = blank % puzzle.cols;
            const bool blank_inside_patch = blank_row >= row && blank_row <= row + 2 &&
                                            blank_col >= puzzle.cols - 2;
            if (!blank_inside_patch) {
                route_blank_to(board, blank, pair_blank_cell, -1, puzzle, locked,
                               protected_cells, workspace, moves, total_cost, false);
            }
            protected_cells[static_cast<std::size_t>(stage_a)] = false;
            protected_cells[static_cast<std::size_t>(stage_b)] = false;
            solve_pair_patch(board, blank, puzzle, row, puzzle.cols - 2, 3, 2,
                             tile_a, tile_b, 0, 1, moves, total_cost);
        };
        bool pair_solved = false;
        std::string macro_error;
        for (bool stage_a_first : {false, true}) {
            try {
                run_top_pair_macro(stage_a_first);
                pair_solved = true;
                break;
            } catch (const std::runtime_error& error) {
                if (macro_error.empty()) macro_error = error.what();
                board = board_before_pair;
                blank = blank_before_pair;
                moves.resize(moves_before_pair);
                total_cost = cost_before_pair;
                protected_cells[static_cast<std::size_t>(stage_a)] = false;
                protected_cells[static_cast<std::size_t>(stage_b)] = false;
            }
        }
        if (!pair_solved) {
            const std::size_t active_cells = static_cast<std::size_t>(std::count(locked.begin(), locked.end(), false));
            if (active_cells <= 200) {
                solve_pair_in_two_row_strip(board, blank, puzzle, locked, tile_a, tile_b,
                                            goal_a, goal_b, moves, total_cost);
            } else {
                throw std::runtime_error("row-by-row solver: both local pair macros failed on a large active area: " + macro_error);
            }
        }
        locked[static_cast<std::size_t>(goal_a)] = true;
        locked[static_cast<std::size_t>(goal_b)] = true;
    }

    // The remaining board has two rows. Place vertical tile pairs from left to right.
    const int bottom_row = puzzle.rows - 2;
    for (int col = 0; col < puzzle.cols - 2; ++col) {
        const int goal_a = bottom_row * puzzle.cols + col;
        const int goal_b = (bottom_row + 1) * puzzle.cols + col;
        const int tile_a = goal_a + 1;
        const int tile_b = goal_b + 1;
        const int stage_a = bottom_row * puzzle.cols + col + 1;
        const int stage_b = (bottom_row + 1) * puzzle.cols + col + 1;
        // Usually a short pair macro is enough. Some reachable configurations put
        // the blank on the other side of a temporary bottleneck; roll back and use
        // an exact search over (tile A, tile B, blank) in the remaining 2xK strip.
        const Board board_before_pair = board;
        const int blank_before_pair = blank;
        const std::size_t moves_before_pair = moves.size();
        const Cost cost_before_pair = total_cost;
        bool macro_succeeded = false;
        try {
            place_tile(board, blank, tile_b, stage_b, puzzle, locked, protected_cells,
                       workspace, moves, total_cost);
            protected_cells[static_cast<std::size_t>(stage_b)] = true;
            const std::vector<int> patch_cells{
                bottom_row * puzzle.cols + col, bottom_row * puzzle.cols + col + 1,
                bottom_row * puzzle.cols + col + 2,
                (bottom_row + 1) * puzzle.cols + col,
                (bottom_row + 1) * puzzle.cols + col + 1,
                (bottom_row + 1) * puzzle.cols + col + 2};
            place_tile_and_blank_in_patch(board, blank, tile_a, stage_a, puzzle, locked,
                                          protected_cells, patch_cells, moves, total_cost);
            protected_cells[static_cast<std::size_t>(stage_b)] = false;
            solve_pair_patch(board, blank, puzzle, bottom_row, col, 2, 3,
                             tile_a, tile_b, 0, 3, moves, total_cost);
            macro_succeeded = true;
        } catch (const std::runtime_error&) {
            board = board_before_pair;
            blank = blank_before_pair;
            moves.resize(moves_before_pair);
            total_cost = cost_before_pair;
            protected_cells[static_cast<std::size_t>(stage_b)] = false;
        }
        if (!macro_succeeded) {
            solve_pair_in_two_row_strip(board, blank, puzzle, locked, tile_a, tile_b,
                                        goal_a, goal_b, moves, total_cost);
        }
        locked[static_cast<std::size_t>(goal_a)] = true;
        locked[static_cast<std::size_t>(goal_b)] = true;
    }

    solve_final_two_by_two(board, blank, puzzle, puzzle.rows - 2, puzzle.cols - 2,
                           moves, total_cost);
    if (board != goal) throw std::runtime_error("row-by-row solver: internal verification failed; final board is not solved");
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return {std::move(moves), total_cost, workspace.visited_total, elapsed};
}

} // namespace pyatnashki
