#include "pyatnashki/types.hpp"

#include <fstream>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace pyatnashki {

int direction_index(char direction) {
    switch (direction) {
    case 'U': return 0;
    case 'D': return 1;
    case 'L': return 2;
    case 'R': return 3;
    default: throw std::runtime_error("direction must be U, D, L, or R");
    }
}

Cost score_for(Cost tile_count, Cost total_cost) {
    const Cost base = 100 * tile_count;
    if (total_cost > 0 && base < std::numeric_limits<Cost>::min() + total_cost) {
        throw std::runtime_error("score overflow");
    }
    return base - total_cost;
}

std::string upper(std::string value) {
    for (char& ch : value) {
        if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
    }
    return value;
}

std::vector<std::string> tokens_from_file(const std::string& path) {
    std::ifstream input(std::filesystem::u8path(path));
    if (!input) throw std::runtime_error("cannot open input file: " + path);

    std::vector<std::string> tokens;
    std::string line;
    while (std::getline(input, line)) {
        if (tokens.empty() && line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
        const auto comment = line.find("--");
        if (comment != std::string::npos) line.erase(comment);
        std::string token;
        for (char ch : line) {
            if (ch == '/' || ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
                if (!token.empty()) {
                    tokens.push_back(token);
                    token.clear();
                }
                if (ch == '/') tokens.emplace_back("/");
            } else {
                token.push_back(ch);
            }
        }
        if (!token.empty()) tokens.push_back(token);
    }
    return tokens;
}

long long parse_integer(const std::string& token, const std::string& what) {
    std::size_t consumed = 0;
    long long value;
    try {
        value = std::stoll(token, &consumed);
    } catch (const std::exception&) {
        throw std::runtime_error("expected integer for " + what + ", got '" + token + "'");
    }
    if (consumed != token.size()) {
        throw std::runtime_error("expected integer for " + what + ", got '" + token + "'");
    }
    return value;
}

Puzzle parse_puzzle(const std::string& path) {
    auto tokens = tokens_from_file(path);
    if (tokens.empty()) throw std::runtime_error("input file is empty");
    // Simplified repository format: U D L R width height cells...
    if ((tokens[0][0] >= '0' && tokens[0][0] <= '9') || tokens[0][0] == '-' || tokens[0][0] == '+') {
        if (tokens.size() < 8) throw std::runtime_error("simplified input requires costs, width, height and cells");
        std::vector<std::string> expanded{"COST", tokens[0], tokens[1], tokens[2], tokens[3],
                                          "/", "DATA", "SIZE", tokens[5], tokens[4]};
        expanded.insert(expanded.end(), tokens.begin() + 6, tokens.end());
        expanded.push_back("/");
        tokens = std::move(expanded);
    }

    std::vector<std::string> data;
    std::vector<std::string> cost_values;
    std::vector<std::string> order_values;
    bool seen_data = false;
    bool seen_cost = false;
    bool seen_order = false;

    for (std::size_t pos = 0; pos < tokens.size();) {
        const std::string section = upper(tokens[pos++]);
        if (section != "DATA" && section != "COST" && section != "ORDER") {
            throw std::runtime_error("unknown block or extra token: " + section);
        }
        bool* seen = section == "DATA" ? &seen_data : section == "COST" ? &seen_cost : &seen_order;
        if (*seen) throw std::runtime_error("block " + section + " occurs more than once");
        *seen = true;
        std::vector<std::string>* target =
            section == "DATA" ? &data : section == "COST" ? &cost_values : &order_values;
        while (pos < tokens.size() && tokens[pos] != "/") target->push_back(tokens[pos++]);
        if (pos == tokens.size()) throw std::runtime_error("block " + section + " is missing '/'");
        ++pos;
    }
    if (!seen_data) throw std::runtime_error("required DATA block is missing");

    if (data.size() < 4 || upper(data[0]) != "SIZE") {
        throw std::runtime_error("DATA must start with 'SIZE N M' followed by the board");
    }
    const long long rows = parse_integer(data[1], "row count");
    const long long cols = parse_integer(data[2], "column count");
    if (rows < 1 || cols < 1 || rows > 100 || cols > 100 ||
        rows * cols < 2 || rows * cols > 10000) {
        throw std::runtime_error("invalid dimensions: require 1<=N,M<=100 and 2<=N*M<=10000");
    }

    Puzzle puzzle;
    puzzle.rows = static_cast<int>(rows);
    puzzle.cols = static_cast<int>(cols);
    const std::size_t cells = static_cast<std::size_t>(rows * cols);
    if (data.size() != cells + 3) {
        throw std::runtime_error("board value count does not match SIZE");
    }
    puzzle.start.reserve(cells);
    for (std::size_t i = 3; i < data.size(); ++i) {
        const long long value = data[i] == "=" ? 0 : parse_integer(data[i], "tile");
        if (value < 0 || static_cast<std::size_t>(value) >= cells) {
            throw std::runtime_error("board values must be in the range 0..N*M-1");
        }
        puzzle.start.push_back(static_cast<int>(value));
    }
    std::vector<bool> seen(cells, false);
    for (int value : puzzle.start) {
        if (value < 0 || static_cast<std::size_t>(value) >= cells || seen[static_cast<std::size_t>(value)]) {
            throw std::runtime_error("board must contain each value 0..N*M-1 exactly once");
        }
        seen[static_cast<std::size_t>(value)] = true;
    }

    if (seen_cost) {
        if (cost_values.size() != 4) throw std::runtime_error("COST must contain exactly U D L R values");
        for (std::size_t i = 0; i < 4; ++i) {
            puzzle.costs[i] = parse_integer(cost_values[i], "move cost");
            if (puzzle.costs[i] < 0) throw std::runtime_error("move costs must be nonnegative");
        }
        puzzle.has_cost_block = true;
    }
    if (seen_order) {
        if (order_values.size() % 2 != 0) throw std::runtime_error("ORDER must contain tile/direction pairs");
        puzzle.has_order = true;
        for (std::size_t i = 0; i < order_values.size(); i += 2) {
            const auto tile = parse_integer(order_values[i], "tile in ORDER");
            const auto direction = upper(order_values[i + 1]);
            if (tile < 1 || tile >= static_cast<long long>(cells) || direction.size() != 1 ||
                std::string("UDLR").find(direction[0]) == std::string::npos) {
                throw std::runtime_error("invalid tile/direction pair in ORDER");
            }
            puzzle.order.push_back({static_cast<int>(tile), direction[0]});
        }
    }
    return puzzle;
}

} // namespace pyatnashki
