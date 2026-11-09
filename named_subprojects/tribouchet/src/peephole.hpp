// Post-optimisation of a finished solution with the move table.
#pragma once

#include <cstdint>
#include <vector>

#include "common.hpp"
#include "move_table.hpp"
#include "problem.hpp"

namespace sp {

// Scans the solution left to right; whenever the last L slides (L <= table depth) have the same
// swap as a stored move that is cheaper (or equally cheap and shorter) and legal at that spot,
// the stretch is replaced. Identity stretches (e.g. U then D) vanish. The effect on the board is
// identical by construction. Stops early at `deadline`. Returns the number of replacements.
size_t optimizeSlides(const Problem& p, const MoveTable& table, std::vector<uint8_t>& slides,
                      Clock::time_point deadline);

}  // namespace sp
