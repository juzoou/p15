#include "pyatnashki/types.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <cmath>
#include <limits>
#include <queue>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pyatnashki {
constexpr Cost INF = std::numeric_limits<Cost>::max();
constexpr std::size_t NO_PARENT = std::numeric_limits<std::size_t>::max();
struct SearchNode {
    Board board; int blank = 0; Cost g = 0; Cost h = 0;
    std::size_t parent = NO_PARENT; Move move{0, ' '};
};
struct BestRecord { Cost cost; std::size_t node; };
struct Neighbor { Board board; int blank; int tile; char direction; Cost cost; };

Cost tile_heuristic(int tile, int index, const Puzzle& puzzle) {
    if (tile == 0) return 0;
    const int goal = tile - 1;
    const int row_delta = index / puzzle.cols - goal / puzzle.cols;
    const int col_delta = index % puzzle.cols - goal % puzzle.cols;
    const auto multiply = [](Cost cost, int distance) {
        if (distance != 0 && cost > INF / distance) throw std::runtime_error("heuristic cost overflow");
        return cost * distance;
    };
    const Cost vertical = row_delta > 0 ? multiply(puzzle.costs[0], row_delta)
                                         : multiply(puzzle.costs[1], -row_delta);
    const Cost horizontal = col_delta > 0 ? multiply(puzzle.costs[2], col_delta)
                                           : multiply(puzzle.costs[3], -col_delta);
    if (vertical > INF - horizontal) throw std::runtime_error("heuristic cost overflow");
    return vertical + horizontal;
}

Cost heuristic(const Board& board, const Puzzle& puzzle) {
    Cost result = 0;
    for (std::size_t i = 0; i < board.size(); ++i) {
        const Cost part = tile_heuristic(board[i], static_cast<int>(i), puzzle);
        if (result > INF - part) throw std::runtime_error("heuristic cost overflow");
        result += part;
    }
    return result;
}

std::vector<Neighbor> neighbors(const Board& board, int blank, const Puzzle& puzzle, bool reverse = false) {
    const int row = blank / puzzle.cols;
    const int col = blank % puzzle.cols;
    std::vector<std::pair<int, char>> candidates;
    if (!reverse) {
        if (row + 1 < puzzle.rows) candidates.emplace_back(blank + puzzle.cols, 'U');
        if (row > 0) candidates.emplace_back(blank - puzzle.cols, 'D');
        if (col + 1 < puzzle.cols) candidates.emplace_back(blank + 1, 'L');
        if (col > 0) candidates.emplace_back(blank - 1, 'R');
    } else {
        if (row + 1 < puzzle.rows) candidates.emplace_back(blank + puzzle.cols, 'D');
        if (row > 0) candidates.emplace_back(blank - puzzle.cols, 'U');
        if (col + 1 < puzzle.cols) candidates.emplace_back(blank + 1, 'R');
        if (col > 0) candidates.emplace_back(blank - 1, 'L');
    }

    std::vector<Neighbor> result;
    result.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        const int tile_index = candidate.first;
        const int tile = board[tile_index];
        Board next = board;
        std::swap(next[blank], next[tile_index]);
        result.push_back({std::move(next), tile_index, tile, candidate.second,
                          puzzle.costs[static_cast<std::size_t>(direction_index(candidate.second))]});
    }
    return result;
}

std::vector<Move> reconstruct(const std::vector<SearchNode>& nodes, std::size_t node) {
    std::vector<Move> path;
    while (nodes[node].parent != NO_PARENT) {
        path.push_back(nodes[node].move);
        node = nodes[node].parent;
    }
    std::reverse(path.begin(), path.end());
    return path;
}

static SearchResult best_first_weighted_impl(const Puzzle& puzzle, std::uint64_t max_expanded, bool use_heuristic, double weight) {
    struct QueueItem {
        long double f;
        Cost g;
        std::uint64_t order;
        std::size_t node;
        bool operator>(const QueueItem& other) const {
            if (f != other.f) return f > other.f;
            if (g != other.g) return g < other.g;
            return order > other.order;
        }
    };
    const auto started = std::chrono::steady_clock::now();
    const Board goal = puzzle.goal();
    const int blank = static_cast<int>(std::find(puzzle.start.begin(), puzzle.start.end(), 0) - puzzle.start.begin());
    std::vector<SearchNode> nodes{{puzzle.start, blank, 0, use_heuristic ? heuristic(puzzle.start, puzzle) : 0,
                                   NO_PARENT, {0, ' '}}};
    std::priority_queue<QueueItem, std::vector<QueueItem>, std::greater<QueueItem>> queue;
    std::uint64_t order = 0;
    queue.push({static_cast<long double>(nodes[0].g) + weight * nodes[0].h, 0, order++, 0});
    std::unordered_map<Board, BestRecord, BoardHash> best;
    best.emplace(puzzle.start, BestRecord{0, 0});
    std::uint64_t expanded = 0;

    while (!queue.empty()) {
        const QueueItem item = queue.top();
        queue.pop();
        const SearchNode current = nodes[item.node];
        const auto known = best.find(current.board);
        if (known == best.end() || known->second.node != item.node) continue;
        if (current.board == goal) {
            const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            return {reconstruct(nodes, item.node), current.g, expanded, elapsed};
        }
        if (++expanded > max_expanded) throw std::runtime_error("search expansion limit exceeded");

        for (auto& next : neighbors(current.board, current.blank, puzzle)) {
            if (current.g > INF - next.cost) throw std::runtime_error("path cost overflow");
            const Cost next_g = current.g + next.cost;
            const auto old = best.find(next.board);
            if (old != best.end() && next_g >= old->second.cost) continue;
            const Cost next_h = use_heuristic ? heuristic(next.board, puzzle) : 0;
            const std::size_t id = nodes.size();
            nodes.push_back({std::move(next.board), next.blank, next_g, next_h, item.node,
                             {next.tile, next.direction}});
            best[nodes.back().board] = {next_g, id};
            if (next_g > INF - next_h) throw std::runtime_error("priority cost overflow");
            queue.push({static_cast<long double>(next_g) + weight * next_h, next_g, order++, id});
        }
    }
    throw std::runtime_error("no solution exists");
}

SearchResult best_first(const Puzzle& puzzle, std::uint64_t max_expanded, bool use_heuristic) {
    return best_first_weighted_impl(puzzle, max_expanded, use_heuristic, 1.0);
}

SearchResult weighted_a_star(const Puzzle& puzzle, std::uint64_t max_expanded, double weight) {
    if (!(weight >= 1.0) || !std::isfinite(weight))
        throw std::runtime_error("Weighted A* weight must be finite and >= 1.0");
    return best_first_weighted_impl(puzzle, max_expanded, true, weight);
}

SearchResult ida_star(const Puzzle& puzzle, std::uint64_t max_expanded) {
    const auto started = std::chrono::steady_clock::now();
    const Board goal = puzzle.goal();
    const int blank = static_cast<int>(std::find(puzzle.start.begin(), puzzle.start.end(), 0) - puzzle.start.begin());
    std::uint64_t expanded = 0;
    Cost bound = heuristic(puzzle.start, puzzle);
    std::vector<Move> path;
    std::vector<Move> solution;
    std::unordered_map<Board, bool, BoardHash> on_path;
    on_path.emplace(puzzle.start, true);
    bool found = false;
    Cost solution_cost = 0;

    std::function<Cost(const Board&, int, Cost)> search =
        [&](const Board& board, int blank_index, Cost g) -> Cost {
        const Cost h = heuristic(board, puzzle);
        if (g > INF - h) throw std::runtime_error("priority cost overflow");
        const Cost f = g + h;
        if (f > bound) return f;
        if (board == goal) {
            found = true;
            solution = path;
            solution_cost = g;
            return g;
        }
        if (++expanded > max_expanded) throw std::runtime_error("search expansion limit exceeded");

        auto next_states = neighbors(board, blank_index, puzzle);
        std::stable_sort(next_states.begin(), next_states.end(), [&](const Neighbor& left, const Neighbor& right) {
            if (g > INF - left.cost || g > INF - right.cost) {
                throw std::runtime_error("path cost overflow");
            }
            const Cost left_g = g + left.cost;
            const Cost right_g = g + right.cost;
            const Cost left_h = heuristic(left.board, puzzle);
            const Cost right_h = heuristic(right.board, puzzle);
            if (left_g > INF - left_h || right_g > INF - right_h) {
                throw std::runtime_error("priority cost overflow");
            }
            return left_g + left_h < right_g + right_h;
        });

        Cost next_bound = INF;
        for (const Neighbor& next : next_states) {
            if (on_path.find(next.board) != on_path.end()) continue;
            if (g > INF - next.cost) throw std::runtime_error("path cost overflow");
            const Cost next_g = g + next.cost;
            on_path.emplace(next.board, true);
            path.push_back({next.tile, next.direction});
            const Cost candidate = search(next.board, next.blank, next_g);
            if (found) return candidate;
            path.pop_back();
            on_path.erase(next.board);
            next_bound = std::min(next_bound, candidate);
        }
        return next_bound;
    };

    for (;;) {
        const Cost next_bound = search(puzzle.start, blank, 0);
        if (found) {
            const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            return {std::move(solution), solution_cost, expanded, elapsed};
        }
        if (next_bound == INF || next_bound <= bound) throw std::runtime_error("no solution exists");
        bound = next_bound;
    }
}

SearchResult zero_one_bfs(const Puzzle& puzzle, std::uint64_t max_expanded) {
    if (std::any_of(puzzle.costs.begin(), puzzle.costs.end(), [](Cost cost) { return cost > 1; })) {
        throw std::runtime_error("0-1 BFS requires every move cost to be 0 or 1");
    }
    const auto started = std::chrono::steady_clock::now();
    const Board goal = puzzle.goal();
    const int blank = static_cast<int>(std::find(puzzle.start.begin(), puzzle.start.end(), 0) - puzzle.start.begin());
    std::vector<SearchNode> nodes{{puzzle.start, blank, 0, 0, NO_PARENT, {0, ' '}}};
    std::deque<std::size_t> queue{0};
    std::unordered_map<Board, BestRecord, BoardHash> best;
    best.emplace(puzzle.start, BestRecord{0, 0});
    std::uint64_t expanded = 0;

    while (!queue.empty()) {
        const std::size_t id = queue.front();
        queue.pop_front();
        const SearchNode current = nodes[id];
        const auto known = best.find(current.board);
        if (known == best.end() || known->second.node != id) continue;
        if (current.board == goal) {
            const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            return {reconstruct(nodes, id), current.g, expanded, elapsed};
        }
        if (++expanded > max_expanded) throw std::runtime_error("search expansion limit exceeded");

        for (auto& next : neighbors(current.board, current.blank, puzzle)) {
            if (current.g > INF - next.cost) throw std::runtime_error("path cost overflow");
            const Cost next_g = current.g + next.cost;
            const auto old = best.find(next.board);
            if (old != best.end() && next_g >= old->second.cost) continue;
            const std::size_t next_id = nodes.size();
            nodes.push_back({std::move(next.board), next.blank, next_g, 0, id, {next.tile, next.direction}});
            best[nodes.back().board] = {next_g, next_id};
            if (next.cost == 0) queue.push_front(next_id);
            else queue.push_back(next_id);
        }
    }
    throw std::runtime_error("no solution exists");
}

SearchResult dial(const Puzzle& puzzle, std::uint64_t max_expanded) {
    const Cost max_edge = *std::max_element(puzzle.costs.begin(), puzzle.costs.end());
    constexpr Cost MAX_BUCKETS = 100000;
    if (max_edge > MAX_BUCKETS) {
        throw std::runtime_error("Dial requires a small maximum edge cost (at most 100,000 here)");
    }
    const auto started = std::chrono::steady_clock::now();
    const Board goal = puzzle.goal();
    const int blank = static_cast<int>(std::find(puzzle.start.begin(), puzzle.start.end(), 0) - puzzle.start.begin());
    std::vector<SearchNode> nodes{{puzzle.start, blank, 0, 0, NO_PARENT, {0, ' '}}};
    std::vector<std::deque<std::size_t>> buckets(static_cast<std::size_t>(max_edge + 1));
    buckets[0].push_back(0);
    std::unordered_map<Board, BestRecord, BoardHash> best;
    best.emplace(puzzle.start, BestRecord{0, 0});
    std::uint64_t expanded = 0;
    std::uint64_t pending = 1;
    Cost current_distance = 0;

    while (pending > 0) {
        auto& bucket = buckets[static_cast<std::size_t>(current_distance % static_cast<Cost>(buckets.size()))];
        if (bucket.empty()) {
            ++current_distance;
            continue;
        }
        const std::size_t id = bucket.front();
        bucket.pop_front();
        --pending;
        const SearchNode current = nodes[id];
        const auto known = best.find(current.board);
        if (known == best.end() || known->second.node != id || current.g != current_distance) continue;
        if (current.board == goal) {
            const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            return {reconstruct(nodes, id), current.g, expanded, elapsed};
        }
        if (++expanded > max_expanded) throw std::runtime_error("search expansion limit exceeded");

        for (auto& next : neighbors(current.board, current.blank, puzzle)) {
            if (current.g > INF - next.cost) throw std::runtime_error("path cost overflow");
            const Cost next_g = current.g + next.cost;
            const auto old = best.find(next.board);
            if (old != best.end() && next_g >= old->second.cost) continue;
            const std::size_t next_id = nodes.size();
            nodes.push_back({std::move(next.board), next.blank, next_g, 0, id, {next.tile, next.direction}});
            best[nodes.back().board] = {next_g, next_id};
            buckets[static_cast<std::size_t>(next_g % static_cast<Cost>(buckets.size()))].push_back(next_id);
            ++pending;
        }
    }
    throw std::runtime_error("no solution exists");
}

SearchResult bidirectional_dijkstra(const Puzzle& puzzle, std::uint64_t max_expanded) {
    struct QueueItem {
        Cost cost;
        std::uint64_t order;
        std::size_t node;
        bool operator>(const QueueItem& other) const {
            if (cost != other.cost) return cost > other.cost;
            return order > other.order;
        }
    };
    const auto started = std::chrono::steady_clock::now();
    const Board goal = puzzle.goal();
    const int start_blank = static_cast<int>(std::find(puzzle.start.begin(), puzzle.start.end(), 0) - puzzle.start.begin());
    const int goal_blank = static_cast<int>(goal.size()) - 1;
    std::vector<SearchNode> forward{{puzzle.start, start_blank, 0, 0, NO_PARENT, {0, ' '}}};
    std::vector<SearchNode> backward{{goal, goal_blank, 0, 0, NO_PARENT, {0, ' '}}};
    std::unordered_map<Board, BestRecord, BoardHash> from_start, from_goal;
    from_start.emplace(puzzle.start, BestRecord{0, 0});
    from_goal.emplace(goal, BestRecord{0, 0});
    std::priority_queue<QueueItem, std::vector<QueueItem>, std::greater<QueueItem>> qf, qb;
    std::uint64_t order = 0;
    qf.push({0, order++, 0});
    qb.push({0, order++, 0});
    Cost best_path = INF;
    std::size_t meet_forward = NO_PARENT, meet_backward = NO_PARENT;
    std::uint64_t expanded = 0;
    if (puzzle.start == goal) {
        const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        return {{}, 0, 0, elapsed};
    }

    auto trim = [](auto& queue, const auto& nodes, const auto& best) {
        while (!queue.empty()) {
            const auto& item = queue.top();
            const auto& node = nodes[item.node];
            const auto found = best.find(node.board);
            if (found != best.end() && found->second.node == item.node) break;
            queue.pop();
        }
    };
    auto consider_meeting = [&](const Board& board) {
        const auto f = from_start.find(board);
        const auto b = from_goal.find(board);
        if (f == from_start.end() || b == from_goal.end()) return;
        if (f->second.cost > INF - b->second.cost) return;
        const Cost total = f->second.cost + b->second.cost;
        if (total < best_path) {
            best_path = total;
            meet_forward = f->second.node;
            meet_backward = b->second.node;
        }
    };

    while (!qf.empty() && !qb.empty()) {
        trim(qf, forward, from_start);
        trim(qb, backward, from_goal);
        if (qf.empty() || qb.empty()) break;
        if (best_path != INF && qf.top().cost <= INF - qb.top().cost &&
            qf.top().cost + qb.top().cost >= best_path) break;
        const bool expand_forward = qf.top().cost <= qb.top().cost;
        auto& queue = expand_forward ? qf : qb;
        auto& nodes = expand_forward ? forward : backward;
        auto& own = expand_forward ? from_start : from_goal;
        const auto& other = expand_forward ? from_goal : from_start;
        const auto item = queue.top();
        queue.pop();
        const SearchNode current = nodes[item.node];
        if (++expanded > max_expanded) throw std::runtime_error("search expansion limit exceeded");

        for (auto& next : neighbors(current.board, current.blank, puzzle, !expand_forward)) {
            if (current.g > INF - next.cost) throw std::runtime_error("path cost overflow");
            const Cost next_g = current.g + next.cost;
            const auto old = own.find(next.board);
            if (old != own.end() && next_g >= old->second.cost) continue;
            const std::size_t next_id = nodes.size();
            nodes.push_back({std::move(next.board), next.blank, next_g, 0, item.node, {next.tile, next.direction}});
            own[nodes.back().board] = {next_g, next_id};
            queue.push({next_g, order++, next_id});
            if (other.find(nodes.back().board) != other.end()) consider_meeting(nodes.back().board);
        }
    }
    if (best_path == INF || meet_forward == NO_PARENT || meet_backward == NO_PARENT) {
        throw std::runtime_error("no solution exists");
    }
    auto path = reconstruct(forward, meet_forward);
    std::size_t node = meet_backward;
    while (backward[node].parent != NO_PARENT) {
        path.push_back(backward[node].move);
        node = backward[node].parent;
    }
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return {std::move(path), best_path, expanded, elapsed};
}

} // namespace pyatnashki
