// Stage 1: precomputed "moves" (empty-cell paths) and their "swaps" (sparse net effect).
#pragma once

#include <cstdint>
#include <vector>

#include "common.hpp"

namespace sp {

// One entry of a swap. Offsets are relative to the empty cell's start position.
// After the move, the cell (dr,dc) holds what was at (sr,sc) before it.
// The pair with (sr,sc) == (0,0) is the empty cell itself: it ends at (dr,dc).
// Only cells whose content actually changes are stored (sparse).
struct Pair {
    int8_t dr, dc, sr, sc;
};

struct MoveRec {
    int64_t cost;       // runtime directional cost of the whole move
    uint64_t path;      // slide i (tile direction) in bits [2i, 2i+1]
    uint32_t pairOff;   // index of the first Pair in MoveTable::pairs
    uint8_t pairCnt;
    uint8_t len;        // number of slides
    uint8_t mUp, mDown, mLeft, mRight;  // margin the empty cell needs from each board edge
    int8_t endDr, endDc;                // final offset of the empty cell
};

struct MoveTable {
    // Sorted by `len`; moves[0] is the identity (len 0) and is never a candidate.
    std::vector<MoveRec> moves;
    std::vector<Pair> pairs;
    // Moves with len == l occupy [lenStart[l], lenStart[l+1]). Size maxLen + 2.
    std::vector<uint32_t> lenStart;
    int maxLen = 0;          // deepest level present (may be < requested depth)
    bool truncated = false;  // generation stopped early (time or memory)

    bool legalAt(const MoveRec& m, int er, int ec, int rows, int cols) const {
        return er >= m.mUp && er + m.mDown < rows && ec >= m.mLeft && ec + m.mRight < cols;
    }
};

constexpr int kMaxDepth = 31;  // path bits are packed into a uint64_t

// Hash of a canonical (sorted by destination) swap; the same function indexes the generator.
uint64_t hashSwap(const Pair* p, int n);

// Builds the sparse swap of an arbitrary slide sequence, one slide at a time. Used by the
// peephole optimiser to look a stretch of a finished solution up in the move table.
struct SwapBuilder {
    Pair a[kMaxDepth + 4];
    int n = 0;
    int er = 0, ec = 0;                 // empty cell, relative to where the sequence started
    void step(int tileDir);             // the tile moves in `tileDir`, the empty cell the other way
    void sorted(Pair* out) const;       // canonical order (by destination cell)
};

// Generates all distinct swaps reachable with up to `depth` slides, keeping for each swap
// only the cheapest move (ties: fewer slides). Stops early at `deadline`.
MoveTable buildMoveTable(const Costs& costs, int depth, Clock::time_point deadline);

}  // namespace sp
