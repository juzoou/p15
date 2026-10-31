#include "parser.h"

#include <fstream>

std::optional<PuzzleInput> parse_file(const std::filesystem::path& p) {
    std::ifstream file(p);
    if (!file) return std::nullopt;

    PuzzleInput puzzle;
    for (auto& weight : puzzle.weights) {
        int64_t v;
        if (!(file >> v) || v < 0 || v > 1'000'000'000) return std::nullopt;
        weight = static_cast<uint32_t>(v);
    }

    int64_t rows, cols;
    if (!(file >> rows >> cols) || rows < 1 || cols < 1 || rows > 100 || cols > 100)
        return std::nullopt;
    const auto n = static_cast<std::size_t>(rows * cols);
    if (n < 2 || n > PuzzleInput::MAX_CELLS) return std::nullopt;
    puzzle.rows = static_cast<uint32_t>(rows);
    puzzle.cols = static_cast<uint32_t>(cols);

    puzzle.cells.resize(n);
    std::vector<bool> seen(n, false);  // каждое число 0..n-1 ровно один раз
    for (std::size_t i = 0; i < n; ++i) {
        int64_t v;
        if (!(file >> v) || v < 0 || static_cast<std::size_t>(v) >= n || seen[v])
            return std::nullopt;
        seen[v] = true;
        puzzle.cells[i] = static_cast<uint16_t>(v);
    }
    return puzzle;
}
