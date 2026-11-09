#include "fallback.hpp"

#include <algorithm>
#include <cstdlib>
#include <array>
#include <functional>
#include <queue>
#include <tuple>

namespace sp {

namespace {
int dirForStep(int dr, int dc) {
    for (int d = 0; d < NUM_DIRS; ++d)
        if (EMPTY_STEP[d].dr == dr && EMPTY_STEP[d].dc == dc) return d;
    return 0;
}
}  // namespace

ConstructiveSolver::ConstructiveSolver(int rows, int cols, const Costs& costs, std::vector<uint16_t> board,
                                       int er, int ec)
    : rows_(rows), cols_(cols), n_(rows * cols), costs_(costs), b_(std::move(board)), posOf_(n_ + 1, -1),
      er_(er), ec_(ec), locked_(n_, 0), seen_(n_, 0), closed_(n_, 0), gCost_(n_, 0), gSteps_(n_, 0),
      prevDir_(n_, 0) {
    for (int k = 0; k < n_; ++k)
        if (b_[k]) posOf_[b_[k]] = k;
    // Work only inside the bottom-right rectangle that contains the empty cell and every
    // misplaced tile (at least 2x2). Cells outside hold correct tiles and are locked, so long
    // empty-cell trips can no longer shift them. A fully scrambled board gives the whole board.
    rLo_ = rows_ - 2;
    cLo_ = cols_ - 2;
    for (int k = 0; k < n_; ++k) {
        if (b_[k] == k + 1) continue;
        rLo_ = std::min(rLo_, k / cols_);
        cLo_ = std::min(cLo_, k % cols_);
    }
    for (int k = 0; k < n_; ++k)
        if (k / cols_ < rLo_ || k % cols_ < cLo_) locked_[k] = 1;
    dirDown_ = dirForStep(+1, 0);
    dirUp_ = dirForStep(-1, 0);
    dirRight_ = dirForStep(0, +1);
    dirLeft_ = dirForStep(0, -1);
}

bool ConstructiveSolver::fail(const std::string& msg) {
    err_ = msg;
    return false;
}

void ConstructiveSolver::viewOf(int idx, int& p, int& q) const {
    const int r = idx / cols_, c = idx % cols_;
    if (transposed_) { p = c; q = r; } else { p = r; q = c; }
}

int ConstructiveSolver::dirTo(int fromIdx, int toIdx) const {
    return dirForStep(toIdx / cols_ - fromIdx / cols_, toIdx % cols_ - fromIdx % cols_);
}

// Empty cell moves by EMPTY_STEP[d]; the tile it swaps with moves in direction d.
void ConstructiveSolver::slideEmpty(int d) {
    const int nr = er_ + EMPTY_STEP[d].dr, nc = ec_ + EMPTY_STEP[d].dc;
    const int from = er_ * cols_ + ec_, to = nr * cols_ + nc;
    const uint16_t t = b_[to];
    b_[from] = t;
    b_[to] = 0;
    posOf_[t] = from;
    er_ = nr;
    ec_ = nc;
    slides_.push_back(static_cast<uint8_t>(d));
}

void ConstructiveSolver::slideEmptyTo(int cell) { slideEmpty(dirTo(er_ * cols_ + ec_, cell)); }

// A* for the empty cell from where it is to `target`, never entering locked cells or `avoid`
// (the tile being moved). Cost = directional cost, ties broken by fewer slides.
bool ConstructiveSolver::moveEmptyTo(int target, int avoid) {
    const int start = er_ * cols_ + ec_;
    if (start == target) return true;
    ++stamp_;
    const int tr = target / cols_, tc = target % cols_;
    const int64_t cDown = costs_.c[dirDown_], cUp = costs_.c[dirUp_];
    const int64_t cRight = costs_.c[dirRight_], cLeft = costs_.c[dirLeft_];
    auto heur = [&](int node, int64_t& hc, int& hs) {
        const int dv = tr - node / cols_, dh = tc - node % cols_;
        hc = (dv > 0 ? dv * cDown : -dv * cUp) + (dh > 0 ? dh * cRight : -dh * cLeft);
        hs = std::abs(dv) + std::abs(dh);
    };
    auto worse = [](const HeapItem& a, const HeapItem& b) {  // min-heap on (f, fs, hs)
        if (a.f != b.f) return a.f > b.f;
        if (a.fs != b.fs) return a.fs > b.fs;
        return a.hs > b.hs;
    };

    heap_.clear();
    seen_[start] = stamp_;
    gCost_[start] = 0;
    gSteps_[start] = 0;
    {
        int64_t hc;
        int hs;
        heur(start, hc, hs);
        heap_.push_back({hc, hs, hs, start});
    }
    bool found = false;
    while (!heap_.empty()) {
        std::pop_heap(heap_.begin(), heap_.end(), worse);
        const HeapItem it = heap_.back();
        heap_.pop_back();
        const int node = it.node;
        if (closed_[node] == stamp_) continue;
        closed_[node] = stamp_;
        if (node == target) { found = true; break; }
        const int r = node / cols_, c = node % cols_;
        for (int d = 0; d < NUM_DIRS; ++d) {
            const int nr = r + EMPTY_STEP[d].dr, nc = c + EMPTY_STEP[d].dc;
            if (nr < 0 || nr >= rows_ || nc < 0 || nc >= cols_) continue;
            const int ni = nr * cols_ + nc;
            if (locked_[ni] || ni == avoid || closed_[ni] == stamp_) continue;
            const int64_t ng = gCost_[node] + costs_.c[d];
            const int32_t ns = gSteps_[node] + 1;
            if (seen_[ni] != stamp_ || ng < gCost_[ni] || (ng == gCost_[ni] && ns < gSteps_[ni])) {
                seen_[ni] = stamp_;
                gCost_[ni] = ng;
                gSteps_[ni] = ns;
                prevDir_[ni] = static_cast<uint8_t>(d);
                int64_t hc;
                int hs;
                heur(ni, hc, hs);
                heap_.push_back({ng + hc, static_cast<int32_t>(ns + hs), hs, ni});
                std::push_heap(heap_.begin(), heap_.end(), worse);
            }
        }
    }
    if (!found) return false;
    pathBuf_.clear();
    for (int cur = target; cur != start;) {
        const int d = prevDir_[cur];
        pathBuf_.push_back(static_cast<uint8_t>(d));
        cur -= EMPTY_STEP[d].dr * cols_ + EMPTY_STEP[d].dc;
    }
    for (size_t i = pathBuf_.size(); i-- > 0;) slideEmpty(pathBuf_[i]);
    return true;
}

// Moves `tile` to view cell (pt,qt) one step at a time. Path rule: first down to row pt (only
// needed for a tile parked above its target), then along the line to column qt, then up.
// Returns false when the empty cell cannot reach the cell the tile has to step into.
bool ConstructiveSolver::placeTile(int tile, int pt, int qt) {
    for (;;) {
        const int idx = posOf_[tile];
        int p, q;
        viewOf(idx, p, q);
        if (p == pt && q == qt) return true;
        int np = p, nq = q;
        if (p < pt) np = p + 1;
        else if (q != qt) nq = q + (qt > q ? 1 : -1);
        else np = p - 1;
        const int nxt = cellOf(np, nq);
        if (!moveEmptyTo(nxt, idx)) return false;
        slideEmptyTo(idx);  // empty steps back onto the tile's old cell: the tile moves into nxt
    }
}

// Exhaustive Dijkstra over the positions of the empty cell and the tracked tiles inside a small
// window (all other tiles are interchangeable). Used for the last two cells of a line (only in
// the rare "trapped" configuration) and for the final 2x2 block.
bool ConstructiveSolver::windowSolve(const std::vector<int>& cells, const std::vector<int>& tiles,
                                     const std::vector<int>& targets) {
    const int W = static_cast<int>(cells.size());
    const int K = static_cast<int>(tiles.size());
    auto winIdx = [&](int cell) {
        for (int w = 0; w < W; ++w)
            if (cells[w] == cell) return w;
        return -1;
    };
    std::vector<int> pos(K + 1);
    pos[0] = winIdx(er_ * cols_ + ec_);
    for (int i = 0; i < K; ++i) pos[i + 1] = winIdx(posOf_[tiles[i]]);
    std::vector<int> tgt(K);
    for (int i = 0; i < K; ++i) tgt[i] = winIdx(targets[i]);
    for (int v : pos) if (v < 0) return fail("windowSolve: piece outside window");
    for (int v : tgt) if (v < 0) return fail("windowSolve: target outside window");

    std::vector<std::array<int, NUM_DIRS>> adj(W);
    for (int w = 0; w < W; ++w)
        for (int d = 0; d < NUM_DIRS; ++d) {
            const int r = cells[w] / cols_ + EMPTY_STEP[d].dr, c = cells[w] % cols_ + EMPTY_STEP[d].dc;
            adj[w][d] = (r < 0 || r >= rows_ || c < 0 || c >= cols_) ? -1 : winIdx(r * cols_ + c);
        }

    int total = 1;
    for (int i = 0; i <= K; ++i) total *= W;
    auto encode = [&](const std::vector<int>& s) {
        int code = 0;
        for (int i = K; i >= 0; --i) code = code * W + s[i];
        return code;
    };
    auto decode = [&](int code, std::vector<int>& s) {
        for (int i = 0; i <= K; ++i) { s[i] = code % W; code /= W; }
    };
    const int64_t INF = INT64_MAX;
    std::vector<int64_t> dist(total, INF);
    std::vector<int32_t> steps(total, 0), prev(total, -1);
    std::vector<uint8_t> prevD(total, 0);
    typedef std::tuple<int64_t, int32_t, int> QI;
    std::priority_queue<QI, std::vector<QI>, std::greater<QI>> pq;
    const int s0 = encode(pos);
    dist[s0] = 0;
    pq.push(QI(0, 0, s0));
    std::vector<int> cur(K + 1), nxt(K + 1);
    int goal = -1;
    while (!pq.empty()) {
        const QI top = pq.top();
        pq.pop();
        const int code = std::get<2>(top);
        if (std::get<0>(top) != dist[code] || std::get<1>(top) != steps[code]) continue;
        decode(code, cur);
        bool ok = true;
        for (int i = 0; i < K; ++i) if (cur[i + 1] != tgt[i]) { ok = false; break; }
        if (ok) { goal = code; break; }
        for (int d = 0; d < NUM_DIRS; ++d) {
            const int nb = adj[cur[0]][d];
            if (nb < 0) continue;
            nxt = cur;
            nxt[0] = nb;
            for (int i = 0; i < K; ++i) if (cur[i + 1] == nb) nxt[i + 1] = cur[0];
            const int nc = encode(nxt);
            const int64_t ndist = dist[code] + costs_.c[d];
            const int32_t nst = steps[code] + 1;
            if (ndist < dist[nc] || (ndist == dist[nc] && nst < steps[nc])) {
                dist[nc] = ndist;
                steps[nc] = nst;
                prev[nc] = code;
                prevD[nc] = static_cast<uint8_t>(d);
                pq.push(QI(ndist, nst, nc));
            }
        }
    }
    if (goal < 0) return fail("windowSolve: no solution in window");
    std::vector<uint8_t> dirs;
    for (int c = goal; c != s0; c = prev[c]) dirs.push_back(prevD[c]);
    for (size_t i = dirs.size(); i-- > 0;) slideEmpty(dirs[i]);
    return true;
}

// Solves one line (a row in the normal view, a column in the transposed view): all cells but the
// last two directly, then the last two with the classic corner trick. Needs two more lines behind it.
bool ConstructiveSolver::solveLine(int p0, int qBase, int len) {
    const int a = qBase + len - 2, b = qBase + len - 1;
    for (int q = qBase; q < a; ++q) {
        const int tile = cellOf(p0, q) + 1;
        if (!placeTile(tile, p0, q)) return fail("solveLine: cannot place single tile");
        locked_[cellOf(p0, q)] = 1;
    }
    const int cA = cellOf(p0, a), cB = cellOf(p0, b);
    const int ta = cA + 1, tb = cB + 1;  // tiles that belong on A and B
    if (!(posOf_[ta] == cA && posOf_[tb] == cB)) {
        // 1) tb goes to A, 2) ta goes just below A, 3) rotate both into place.
        if (!placeTile(tb, p0, a)) return fail("solveLine: cannot place corner tile");
        locked_[cA] = 1;
        if (placeTile(ta, p0 + 1, a)) {
            if (!moveEmptyTo(cB, posOf_[ta])) return fail("solveLine: cannot reach corner");
            slideEmptyTo(cA);                 // tb slides into B
            slideEmptyTo(cellOf(p0 + 1, a));  // ta slides up into A
        } else {
            // Trapped corner (empty parked in the pocket B behind ta): solve it exhaustively.
            locked_[cA] = 0;
            std::vector<int> win;
            for (int p = p0; p <= p0 + 2; ++p)
                for (int q = std::max(a - 1, qBase); q <= b; ++q) {
                    const int cell = cellOf(p, q);
                    if (!locked_[cell]) win.push_back(cell);
                }
            if (!windowSolve(win, {ta, tb}, {cA, cB})) return false;
        }
    }
    locked_[cA] = 1;
    locked_[cB] = 1;
    return true;
}

bool ConstructiveSolver::run() {
    // Rows from the top while at least three rows remain (two stay for the column phase).
    transposed_ = false;
    for (int r0 = rLo_; r0 + 2 < rows_; ++r0)
        if (!solveLine(r0, cLo_, cols_ - cLo_)) return false;
    // Columns from the left while at least three columns remain, on the last two rows.
    transposed_ = true;
    for (int c0 = cLo_; c0 + 2 < cols_; ++c0)
        if (!solveLine(c0, rows_ - 2, 2)) return false;
    transposed_ = false;
    // Final 2x2 block.
    std::vector<int> win, tiles, targets;
    for (int r = rows_ - 2; r < rows_; ++r)
        for (int c = cols_ - 2; c < cols_; ++c) {
            const int cell = r * cols_ + c;
            win.push_back(cell);
            if (cell != n_ - 1) { tiles.push_back(cell + 1); targets.push_back(cell); }
        }
    if (!windowSolve(win, tiles, targets)) return false;
    for (int k = 0; k + 1 < n_; ++k)
        if (b_[k] != k + 1) return fail("fallback finished with an unsolved board");
    return b_[n_ - 1] == 0 ? true : fail("fallback: empty cell not in the corner");
}

}  // namespace sp
