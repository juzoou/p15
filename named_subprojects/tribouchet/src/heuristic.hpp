// Heuristic h, Zobrist hash and goal bookkeeping behind one narrow interface.
//
// The beam search only talks to `Scorer`:
//   * fullH / fullHash / fullCorrect : recompute everything from a board;
//   * evaluate()                     : incremental delta of a candidate move, touching only
//                                      the cells of its swap (no board copy).
// To benchmark a cached or a fully recomputed variant, or another heuristic, replace the
// body of this class; nothing else depends on its internals.
#pragma once

#include <cstdint>
#include <vector>

#include "common.hpp"
#include "move_table.hpp"
#include "problem.hpp"

namespace sp {

class Scorer {
public:
    explicit Scorer(const Problem& p)
        : cols_(p.cols), n_(p.n()), costs_(p.costs), tgtR_(n_ + 1, 0), tgtC_(n_ + 1, 0) {
        for (int t = 1; t < n_; ++t) {
            tgtR_[t] = static_cast<int16_t>((t - 1) / cols_);
            tgtC_[t] = static_cast<int16_t>((t - 1) % cols_);
        }
    }

    // Weighted Manhattan distance of one tile standing on (r,c): each unit of distance costs
    // the direction cost of the move that brings the tile closer along that axis.
    // (Linear conflicts are not included; keep this function the only place that defines h.)
    int64_t tileCost(int tile, int r, int c) const {
        const int dv = tgtR_[tile] - r;
        const int dh = tgtC_[tile] - c;
        const int64_t v = dv > 0 ? dv * costs_.c[DIR_D] : -dv * costs_.c[DIR_U];
        const int64_t h = dh > 0 ? dh * costs_.c[DIR_R] : -dh * costs_.c[DIR_L];
        return v + h;
    }

    static uint64_t zobrist(int tile, int cell) {
        return mix64((static_cast<uint64_t>(tile) << 20) ^ static_cast<uint64_t>(cell) ^ 0x5851F42D4C957F2DULL);
    }

    int64_t fullH(const std::vector<uint16_t>& b) const {
        int64_t h = 0;
        for (int k = 0; k < n_; ++k)
            if (b[k]) h += tileCost(b[k], k / cols_, k % cols_);
        return h;
    }
    uint64_t fullHash(const std::vector<uint16_t>& b) const {
        uint64_t h = 0;
        for (int k = 0; k < n_; ++k)
            if (b[k]) h ^= zobrist(b[k], k);
        return h;
    }
    int fullCorrect(const std::vector<uint16_t>& b) const {
        int c = 0;
        for (int k = 0; k < n_; ++k)
            if (b[k] && b[k] == k + 1) ++c;
        return c;
    }
    int goalCorrect() const { return n_ - 1; }

    struct Delta {
        int64_t dh;
        uint64_t dhash;
        int dcorrect;
    };

    // Effect of applying move `m` (stored swap `pairs`) with the empty cell at (er,ec).
    // The move must be legal there. Only the cells listed in the swap are read.
    Delta evaluate(const uint16_t* board, int er, int ec, const MoveRec& m, const Pair* pairs) const {
        Delta d{0, 0, 0};
        for (int i = 0; i < m.pairCnt; ++i) {
            const Pair& p = pairs[i];
            if (p.sr == 0 && p.sc == 0) continue;  // the empty cell itself
            const int sr = er + p.sr, sc = ec + p.sc;
            const int dr = er + p.dr, dc = ec + p.dc;
            const int sIdx = sr * cols_ + sc, dIdx = dr * cols_ + dc;
            const int tile = board[sIdx];
            d.dh += tileCost(tile, dr, dc) - tileCost(tile, sr, sc);
            d.dhash ^= zobrist(tile, sIdx) ^ zobrist(tile, dIdx);
            d.dcorrect += (dIdx == tile - 1) - (sIdx == tile - 1);
        }
        return d;
    }

private:
    int cols_, n_;
    Costs costs_;
    std::vector<int16_t> tgtR_, tgtC_;
};

}  // namespace sp
