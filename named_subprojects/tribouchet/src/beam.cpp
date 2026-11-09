#include "beam.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <unordered_set>

namespace sp {

namespace {

struct Entry {
    std::vector<uint16_t> board;
    int er = 0, ec = 0;
    int64_t g = 0, h = 0;
    uint64_t hash = 0;
    int correct = 0;
    int32_t node = 0;
};

// Solution tree: the move list of an entry is rebuilt from parent pointers.
struct Node {
    int32_t parent;
    int32_t move;
};

struct Cand {
    double score;
    int64_t g, h;
    uint64_t hash;
    int32_t parent, move, correct;
};

// Lower score first; among equal scores prefer the smaller h (deeper in A* terms); then
// parent and move id for full determinism.
bool candLess(const Cand& a, const Cand& b) {
    if (a.score != b.score) return a.score < b.score;
    if (a.h != b.h) return a.h < b.h;
    if (a.parent != b.parent) return a.parent < b.parent;
    return a.move < b.move;
}

// Visited set with a size cap: ring buffer + hash set, oldest entries are evicted first.
class VisitedSet {
public:
    explicit VisitedSet(size_t cap) : ring_(cap ? cap : 1) { set_.reserve(ring_.size() * 2); }
    bool contains(uint64_t h) const { return set_.count(h) != 0; }
    void insert(uint64_t h) {
        if (!set_.insert(h).second) return;
        if (size_ == ring_.size()) {
            set_.erase(ring_[head_]);
        } else {
            ++size_;
        }
        ring_[head_] = h;
        head_ = (head_ + 1) % ring_.size();
    }

private:
    std::vector<uint64_t> ring_;
    std::unordered_set<uint64_t> set_;
    size_t head_ = 0, size_ = 0;
};

void applyMove(std::vector<uint16_t>& b, int cols, int& er, int& ec, const MoveRec& m) {
    for (int i = 0; i < m.len; ++i) {
        const int d = static_cast<int>((m.path >> (2 * i)) & 3u);
        const int nr = er + EMPTY_STEP[d].dr, nc = ec + EMPTY_STEP[d].dc;
        b[er * cols + ec] = b[nr * cols + nc];
        b[nr * cols + nc] = 0;
        er = nr;
        ec = nc;
    }
}

void appendSlides(std::vector<uint8_t>& out, const MoveRec& m) {
    for (int i = 0; i < m.len; ++i) out.push_back(static_cast<uint8_t>((m.path >> (2 * i)) & 3u));
}

}  // namespace

BeamOutcome runBeam(const Problem& p, const MoveTable& table, const Scorer& scorer, const BeamOptions& opt) {
    const int cols = p.cols, rows = p.rows;
    const int B = std::max(1, opt.width);
    BeamOutcome out;

    Entry start;
    start.board = p.cells;
    for (int k = 0; k < p.n(); ++k)
        if (p.cells[k] == 0) { start.er = k / cols; start.ec = k % cols; }
    start.g = 0;
    start.h = scorer.fullH(start.board);
    start.hash = scorer.fullHash(start.board);
    start.correct = scorer.fullCorrect(start.board);
    start.node = 0;

    std::vector<Node> nodes;
    nodes.push_back({-1, -1});

    // Best entry ever seen by h: the constructive fallback continues from it on a stall.
    Entry best = start;

    auto finish = [&](const Entry& e, bool solved) {
        std::vector<int32_t> mv;
        for (int32_t nd = e.node; nd > 0; nd = nodes[nd].parent) mv.push_back(nodes[nd].move);
        std::reverse(mv.begin(), mv.end());
        for (int32_t id : mv) appendSlides(out.slides, table.moves[id]);
        out.board = e.board;
        out.er = e.er;
        out.ec = e.ec;
        out.solved = solved;
    };

    if (table.maxLen < 1 || table.moves.size() < 2) {  // nothing to search with
        finish(start, false);
        return out;
    }

    // Far phase: while h exceeds what the longest move can change, only max-depth moves.
    const int64_t farThreshold = static_cast<int64_t>(table.maxLen) * p.costs.maxCost();
    const uint32_t totalMoves = static_cast<uint32_t>(table.moves.size());
    const uint32_t maxLenStart = table.lenStart[table.maxLen];

    VisitedSet visited(opt.visitedCap);
    visited.insert(start.hash);

    std::vector<Entry> beam;
    beam.push_back(start);
    std::vector<Cand> cands;
    const size_t kCandCap = 1500000, kCandKeep = 200000;

    double bestScore = static_cast<double>(start.g) + opt.weight * static_cast<double>(start.h);
    int64_t bestH = start.h;
    int stall = 0;

    for (;;) {
        // The clock is read once per iteration, never per candidate.
        if (Clock::now() >= opt.deadline) { out.stopReason = "deadline"; break; }
        ++out.iterations;

        cands.clear();
        int32_t solvedIdx = -1;  // cheapest candidate that reaches the target
        for (size_t j = 0; j < beam.size(); ++j) {
            const Entry& e = beam[j];
            const bool far = e.h > farThreshold;
            const uint32_t lo = far ? maxLenStart : 1u;  // index 0 is the identity
            for (uint32_t mi = lo; mi < totalMoves; ++mi) {
                const MoveRec& m = table.moves[mi];
                if (!table.legalAt(m, e.er, e.ec, rows, cols)) continue;
                const Scorer::Delta d = scorer.evaluate(e.board.data(), e.er, e.ec, m, &table.pairs[m.pairOff]);
                Cand c;
                c.g = e.g + m.cost;
                c.h = e.h + d.dh;
                c.hash = e.hash ^ d.dhash;
                c.parent = static_cast<int32_t>(j);
                c.move = static_cast<int32_t>(mi);
                c.correct = e.correct + d.dcorrect;
                c.score = static_cast<double>(c.g) + opt.weight * static_cast<double>(c.h);
                cands.push_back(c);
                if (c.correct == scorer.goalCorrect()) {
                    if (solvedIdx < 0 || c.g < cands[solvedIdx].g) solvedIdx = static_cast<int32_t>(cands.size() - 1);
                }
                if (cands.size() >= kCandCap && solvedIdx < 0) {  // keep memory bounded
                    std::nth_element(cands.begin(), cands.begin() + kCandKeep, cands.end(), candLess);
                    cands.resize(kCandKeep);
                }
            }
        }
        if (cands.empty()) { out.stopReason = "no legal move"; break; }  // stall

        auto materialize = [&](const Cand& c) {
            const Entry& parent = beam[c.parent];
            Entry e;
            e.board = parent.board;
            e.er = parent.er;
            e.ec = parent.ec;
            applyMove(e.board, cols, e.er, e.ec, table.moves[c.move]);
            e.g = c.g;
            e.h = c.h;
            e.hash = c.hash;
            e.correct = c.correct;
            nodes.push_back({parent.node, c.move});
            e.node = static_cast<int32_t>(nodes.size() - 1);
#ifdef SP_VERIFY
            if (scorer.fullH(e.board) != e.h || scorer.fullHash(e.board) != e.hash ||
                scorer.fullCorrect(e.board) != e.correct) {
                std::fprintf(stderr, "SP_VERIFY: incremental evaluation mismatch\n");
                std::abort();
            }
#endif
            return e;
        };

        if (solvedIdx >= 0) {
            Entry e = materialize(cands[solvedIdx]);
            finish(e, true);
            out.stopReason = "solved";
            return out;
        }

        // Rank, drop tabu / duplicate hashes, keep the best B. Ranking is done lazily on a
        // growing prefix, which gives the same result as filtering all candidates first.
        std::vector<Cand> chosen;
        size_t K = std::min(cands.size(), std::max<size_t>(static_cast<size_t>(B) * 4, 32));
        for (;;) {
            if (K < cands.size()) std::nth_element(cands.begin(), cands.begin() + K, cands.end(), candLess);
            std::sort(cands.begin(), cands.begin() + K, candLess);
            chosen.clear();
            for (size_t i = 0; i < K && chosen.size() < static_cast<size_t>(B); ++i) {
                const Cand& c = cands[i];
                if (visited.contains(c.hash)) continue;
                bool dup = false;
                for (const Cand& s : chosen)
                    if (s.hash == c.hash) { dup = true; break; }
                if (!dup) chosen.push_back(c);
            }
            if (chosen.size() == static_cast<size_t>(B) || K == cands.size()) break;
            K = std::min(cands.size(), K * 4);
        }
        if (chosen.empty()) { out.stopReason = "all tabu"; break; }  // stall

        std::vector<Entry> next;
        next.reserve(chosen.size());
        double iterBestScore = chosen[0].score;
        int64_t iterBestH = chosen[0].h;
        for (const Cand& c : chosen) {
            visited.insert(c.hash);
            next.push_back(materialize(c));
            iterBestH = std::min(iterBestH, c.h);
            iterBestScore = std::min(iterBestScore, c.score);
            const Entry& e = next.back();
            if (e.h < best.h || (e.h == best.h && e.g < best.g)) best = e;
        }
        beam.swap(next);

        // Progress = a new best score OR a new best h. (With W = 1 and a consistent h the score
        // g + W*h never decreases, so the score alone would flag every run as stalled.)
        bool progress = false;
        if (iterBestScore < bestScore - 1e-9) { bestScore = iterBestScore; progress = true; }
        if (iterBestH < bestH) { bestH = iterBestH; progress = true; }
        stall = progress ? 0 : stall + 1;
        if (stall >= opt.stallLimit) { out.stopReason = "stall"; break; }
    }

    finish(best, false);
    out.startH = start.h;
    out.endH = best.h;
    return out;
}

}  // namespace sp
