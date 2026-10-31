#include "pyatnashki/types.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace pyatnashki {
namespace { constexpr Cost INF = std::numeric_limits<Cost>::max(); }

bool solvable(const Puzzle& puzzle) {
    std::vector<int> fenwick(puzzle.start.size() + 1, 0);
    auto prefix_sum = [&fenwick](int index) {
        int sum = 0;
        while (index > 0) {
            sum += fenwick[static_cast<std::size_t>(index)];
            index -= index & -index;
        }
        return sum;
    };
    std::uint64_t inversions = 0;
    std::uint64_t seen = 0;
    for (int tile : puzzle.start) {
        if (tile == 0) continue;
        inversions += seen - static_cast<std::uint64_t>(prefix_sum(tile));
        for (int index = tile; index < static_cast<int>(fenwick.size()); index += index & -index) {
            ++fenwick[static_cast<std::size_t>(index)];
        }
        ++seen;
    }
    if (puzzle.rows == 1 || puzzle.cols == 1) {
        int expected = 1;
        for (int tile : puzzle.start) {
            if (tile != 0 && tile != expected++) return false;
        }
        return true;
    }
    if (puzzle.cols % 2 == 1) return inversions % 2 == 0;
    const auto blank = std::find(puzzle.start.begin(), puzzle.start.end(), 0);
    const int row_from_bottom = puzzle.rows - static_cast<int>(blank - puzzle.start.begin()) / puzzle.cols;
    return (inversions + static_cast<std::uint64_t>(row_from_bottom)) % 2 == 1;
}

CheckResult check_order(const Puzzle& puzzle) {
    Board board = puzzle.start;
    int blank = static_cast<int>(std::find(board.begin(), board.end(), 0) - board.begin());
    Cost total = 0;
    for (std::size_t move_number = 0; move_number < puzzle.order.size(); ++move_number) {
        const Move move = puzzle.order[move_number];
        const int row = blank / puzzle.cols;
        const int col = blank % puzzle.cols;
        int source = -1;
        bool legal = false;
        switch (move.direction) {
        case 'U': legal = row + 1 < puzzle.rows; source = blank + puzzle.cols; break;
        case 'D': legal = row > 0; source = blank - puzzle.cols; break;
        case 'L': legal = col + 1 < puzzle.cols; source = blank + 1; break;
        case 'R': legal = col > 0; source = blank - 1; break;
        default: throw std::runtime_error("invalid direction in ORDER");
        }
        if (!legal || board[source] != move.tile) {
            throw std::runtime_error("illegal move " + std::to_string(move_number + 1) + " in ORDER");
        }
        const Cost cost = puzzle.costs[static_cast<std::size_t>(direction_index(move.direction))];
        if (total > INF - cost) throw std::runtime_error("path cost overflow");
        total += cost;
        std::swap(board[blank], board[source]);
        blank = source;
    }
    std::size_t placed = 0;
    for (std::size_t i = 0; i + 1 < board.size(); ++i) {
        if (board[i] == static_cast<int>(i) + 1) ++placed;
    }
    return {std::move(board), placed, total};
}

std::string format_board(const Board& board, int rows, int cols) {
    std::ostringstream output;
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            if (col) output << ' ';
            const int value = board[static_cast<std::size_t>(row * cols + col)];
            if (value == 0) output << '=';
            else output << value;
        }
        if (row + 1 < rows) output << '\n';
    }
    return output.str();
}

std::string render_output(const Puzzle& puzzle, const std::vector<Move>& moves, const std::string& summary) {
    std::ostringstream output;
    output << "DATA\nSIZE " << puzzle.rows << ' ' << puzzle.cols << '\n'
           << format_board(puzzle.start, puzzle.rows, puzzle.cols) << "\n/\nCOST\n";
    for (std::size_t i = 0; i < puzzle.costs.size(); ++i) {
        if (i) output << ' ';
        output << puzzle.costs[i];
    }
    output << "\n/\nORDER\n";
    for (const Move& move : moves) output << move.tile << ' ' << move.direction << '\n';
    output << "/\n-- " << summary << '\n';
    return output.str();
}

void write_text(const std::string& path, const std::string& text) {
    std::ofstream output(std::filesystem::u8path(path));
    if (!output) throw std::runtime_error("cannot open output file: " + path);
    output << text;
    if (!output) throw std::runtime_error("failed writing output file: " + path);
}

} // namespace pyatnashki
