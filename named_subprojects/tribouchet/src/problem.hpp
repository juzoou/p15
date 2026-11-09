// Problem definition, input parsing, solvability test, solution replay/output.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common.hpp"

namespace sp {

struct Problem {
    int rows = 0, cols = 0;
    std::vector<uint16_t> cells;  // row-major; 0 is the empty cell, tiles are 1..n-1
    Costs costs;                  // default 1 1 1 1
    int n() const { return rows * cols; }
};

// Parses the DATA / COST sections. Returns false and fills `err` on malformed
// input. `sawOrder` is set when an ORDER section is present (not supported as
// input); the function returns false in that case as well.
bool parseProblem(const std::string& text, Problem& p, std::string& err, bool& sawOrder);

// True when `cells` equals the target configuration (1..n-1 ascending, empty last).
bool isSolved(const std::vector<uint16_t>& cells);

// Solvability by permutation-cycle count (O(n)); handles the 1 x M / N x 1 case.
bool isSolvable(const Problem& p);

// Replays `slides` (tile directions, in order) on the initial board, checks that
// the final board is the target and builds the complete output text
// (DATA section followed by ORDER section). Returns false if the sequence is
// illegal or does not solve the board. `totalCost` receives the sum of costs.
bool buildSolutionText(const Problem& p, const std::vector<uint8_t>& slides, std::string& out,
                       int64_t& totalCost, std::string& err);

}  // namespace sp
