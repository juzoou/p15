// Stage 2: beam search over swaps with a tabu (visited) filter.
#pragma once

#include <cstdint>
#include <vector>

#include "common.hpp"
#include "heuristic.hpp"
#include "move_table.hpp"
#include "problem.hpp"

namespace sp {

struct BeamOptions {
    int width = 1;               // boards kept per iteration (1 = greedy with tabu)
    double weight = 1.0;         // W in  g + W * h
    int stallLimit = 50;         // iterations without progress that count as a stall
    Clock::time_point deadline;  // beam search must stop (stall) at this time
    size_t visitedCap = size_t(1) << 18;
};

struct BeamOutcome {
    bool solved = false;
    std::vector<uint8_t> slides;  // tile directions leading from the initial board to `board`
    std::vector<uint16_t> board;  // board reached (the target when solved)
    int er = 0, ec = 0;           // empty cell of `board`
    long iterations = 0;
    const char* stopReason = "";   // diagnostics only
    int64_t startH = 0, endH = 0;
};

BeamOutcome runBeam(const Problem& p, const MoveTable& table, const Scorer& scorer, const BeamOptions& opt);

}  // namespace sp
