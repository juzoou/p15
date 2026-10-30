#ifndef PARSER_H
#define PARSER_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

struct PuzzleInput {
    static constexpr std::size_t MAX_CELLS = 10'000;

    uint32_t rows{};                  // первое число после цен
    uint32_t cols{};                  // второе число
    std::array<uint32_t, 4> weights{};  // цены ходов фишки: U D L R
    std::vector<uint16_t> cells;      // 0 = пустая клетка
};

std::optional<PuzzleInput> parse_file(const std::filesystem::path& p);

#endif // PARSER_H
