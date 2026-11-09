// Constructive fallback: finishes ANY solvable board (rows, then columns, then the last 2x2),
// generalised to N x M and to arbitrary directional costs (empty cell routed on cheapest paths).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common.hpp"

namespace sp {

class ConstructiveSolver {
public:
    ConstructiveSolver(int rows, int cols, const Costs& costs, std::vector<uint16_t> board, int er, int ec);

    // Solves the board in place. Returns false only on an internal error (never on a solvable board).
    bool run();

    const std::vector<uint8_t>& slides() const { return slides_; }  // tile directions
    const std::string& error() const { return err_; }

private:
    int rows_, cols_, n_;
    Costs costs_;
    std::vector<uint16_t> b_;
    std::vector<int32_t> posOf_;  // tile -> cell index
    int er_, ec_;
    std::vector<uint8_t> locked_;
    std::vector<uint8_t> slides_;
    std::string err_;
    int rLo_ = 0, cLo_ = 0;  // working region is rows >= rLo_, cols >= cLo_ (everything else is already correct)
    bool transposed_ = false;  // view: (p,q) = (row,col) or, when transposed, (col,row)

    // A* scratch (stamped, so nothing is cleared between searches).
    struct HeapItem {
        int64_t f;
        int32_t fs, hs, node;
    };
    std::vector<uint32_t> seen_, closed_;
    std::vector<int64_t> gCost_;
    std::vector<int32_t> gSteps_;
    std::vector<uint8_t> prevDir_;
    std::vector<HeapItem> heap_;
    std::vector<uint8_t> pathBuf_;
    uint32_t stamp_ = 0;
    int dirDown_ = 0, dirUp_ = 0, dirRight_ = 0, dirLeft_ = 0;  // tile dir whose empty step is ...

    int cellOf(int p, int q) const { return transposed_ ? q * cols_ + p : p * cols_ + q; }
    void viewOf(int idx, int& p, int& q) const;
    int dirTo(int fromIdx, int toIdx) const;

    void slideEmpty(int d);
    void slideEmptyTo(int cell);
    bool moveEmptyTo(int target, int avoid);
    bool placeTile(int tile, int pt, int qt);
    bool windowSolve(const std::vector<int>& cells, const std::vector<int>& tiles, const std::vector<int>& targets);
    bool solveLine(int p0, int qBase, int len);
    bool fail(const std::string& msg);
};

}  // namespace sp
