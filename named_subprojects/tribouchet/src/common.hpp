// Shared basic types and the single place that defines direction conventions.
#pragma once

#include <chrono>
#include <cstdint>
#include <vector>

namespace sp {

using Clock = std::chrono::steady_clock;

// ---------------------------------------------------------------------------
// Direction conventions. A direction names the movement of the TILE (not of the
// empty cell). The order matches the COST line of the input: U D L R.
//
// EMPTY_STEP is the ONLY place that maps a tile direction to the displacement
// of the empty cell: if a tile moves up, the empty cell moves down (row + 1).
// Flip the table below if the convention ever has to change.
// ---------------------------------------------------------------------------
enum Dir : int { DIR_U = 0, DIR_D = 1, DIR_L = 2, DIR_R = 3 };
constexpr int NUM_DIRS = 4;
constexpr char DIR_CHAR[NUM_DIRS] = {'U', 'D', 'L', 'R'};

struct Step {
    int dr, dc;
};
constexpr Step EMPTY_STEP[NUM_DIRS] = {{+1, 0}, {-1, 0}, {0, +1}, {0, -1}};

// Opposite direction (U<->D, L<->R). Relies on the pairing in the enum above.
constexpr int oppositeDir(int d) { return d ^ 1; }

// Per-direction cost of one slide, indexed by Dir (tile direction).
struct Costs {
    int64_t c[NUM_DIRS] = {1, 1, 1, 1};
    int64_t maxCost() const {
        int64_t m = c[0];
        for (int i = 1; i < NUM_DIRS; ++i) m = c[i] > m ? c[i] : m;
        return m;
    }
};

// 64-bit mixer (splitmix64 finaliser); used for Zobrist-style hashing.
inline uint64_t mix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

inline double secondsSince(Clock::time_point t0) {
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

}  // namespace sp
