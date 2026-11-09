#include "move_table.hpp"

#include <algorithm>
#include <cstring>

namespace sp {

namespace {

constexpr int kMaxPairs = kMaxDepth + 4;
constexpr uint32_t kNoSlot = 0xFFFFFFFFu;
constexpr size_t kMaxMoves = 6000000;  // memory safety net (~300 MB at the extreme)

// Working state of the generator: all stored moves plus a hash table keyed by swap.
struct Generator {
    std::vector<MoveRec> mv;
    std::vector<uint64_t> hash;
    std::vector<Pair> pool;
    std::vector<int64_t> expCost;   // cost at last expansion
    std::vector<uint8_t> expLen;    // len at last expansion (255 = never)
    std::vector<uint32_t> slots;
    size_t mask = 0;

    Generator() {
        slots.assign(1u << 12, kNoSlot);
        mask = slots.size() - 1;
    }

    bool sameSwap(uint32_t id, uint64_t h, const Pair* p, int n) const {
        const MoveRec& m = mv[id];
        return hash[id] == h && m.pairCnt == n &&
               (n == 0 || std::memcmp(&pool[m.pairOff], p, sizeof(Pair) * n) == 0);
    }

    // Returns the id of the stored move with this swap, or kNoSlot; `slotOut` is where it
    // would be inserted.
    uint32_t find(uint64_t h, const Pair* p, int n, size_t& slotOut) const {
        size_t s = static_cast<size_t>(h) & mask;
        while (slots[s] != kNoSlot) {
            if (sameSwap(slots[s], h, p, n)) { slotOut = s; return slots[s]; }
            s = (s + 1) & mask;
        }
        slotOut = s;
        return kNoSlot;
    }

    void grow() {
        std::vector<uint32_t> bigger(slots.size() * 2, kNoSlot);
        size_t newMask = bigger.size() - 1;
        for (uint32_t id = 0; id < mv.size(); ++id) {
            size_t s = static_cast<size_t>(hash[id]) & newMask;
            while (bigger[s] != kNoSlot) s = (s + 1) & newMask;
            bigger[s] = id;
        }
        slots.swap(bigger);
        mask = newMask;
    }

    uint32_t add(const MoveRec& m, uint64_t h, const Pair* p, int n, size_t slot) {
        if ((mv.size() + 1) * 2 > slots.size()) {
            grow();
            size_t dummy;
            find(h, p, n, dummy);
            slot = dummy;
        }
        MoveRec r = m;
        r.pairOff = static_cast<uint32_t>(pool.size());
        r.pairCnt = static_cast<uint8_t>(n);
        pool.insert(pool.end(), p, p + n);
        uint32_t id = static_cast<uint32_t>(mv.size());
        mv.push_back(r);
        hash.push_back(h);
        expCost.push_back(0);
        expLen.push_back(255);
        slots[slot] = id;
        return id;
    }
};

// --- tiny helpers working on a sparse swap held in a small array ---------------------
int findDst(const Pair* a, int n, int r, int c) {
    for (int i = 0; i < n; ++i)
        if (a[i].dr == r && a[i].dc == c) return i;
    return -1;
}

// "Cell (r,c) now holds what was at (sr,sc)"; identity entries are dropped.
void setCell(Pair* a, int& n, int r, int c, int sr, int sc) {
    int i = findDst(a, n, r, c);
    if (sr == r && sc == c) {
        if (i >= 0) a[i] = a[--n];
        return;
    }
    if (i < 0) i = n++;
    a[i] = Pair{static_cast<int8_t>(r), static_cast<int8_t>(c), static_cast<int8_t>(sr),
                static_cast<int8_t>(sc)};
}

}  // namespace

uint64_t hashSwap(const Pair* p, int n) {
    uint64_t h = mix64(static_cast<uint64_t>(n) + 0x1234567ULL);
    for (int i = 0; i < n; ++i) {
        uint32_t w = (static_cast<uint32_t>(static_cast<uint8_t>(p[i].dr)) << 24) |
                     (static_cast<uint32_t>(static_cast<uint8_t>(p[i].dc)) << 16) |
                     (static_cast<uint32_t>(static_cast<uint8_t>(p[i].sr)) << 8) |
                     static_cast<uint32_t>(static_cast<uint8_t>(p[i].sc));
        h = mix64(h ^ w) + static_cast<uint64_t>(i);
    }
    return h;
}

void SwapBuilder::step(int d) {
    const int nr = er + EMPTY_STEP[d].dr, nc = ec + EMPTY_STEP[d].dc;
    int tr = nr, tc = nc;
    const int i = findDst(a, n, nr, nc);
    if (i >= 0) { tr = a[i].sr; tc = a[i].sc; }
    setCell(a, n, er, ec, tr, tc);  // old empty cell receives the tile that was at the new one
    setCell(a, n, nr, nc, 0, 0);    // new empty cell receives the empty token
    er = nr;
    ec = nc;
}

void SwapBuilder::sorted(Pair* out) const {
    std::copy(a, a + n, out);
    std::sort(out, out + n, [](const Pair& x, const Pair& y) {
        return x.dr != y.dr ? x.dr < y.dr : x.dc < y.dc;
    });
}

MoveTable buildMoveTable(const Costs& costs, int depth, Clock::time_point deadline) {
    if (depth > kMaxDepth) depth = kMaxDepth;
    if (depth < 0) depth = 0;

    Generator g;
    MoveRec root{};
    root.cost = 0;
    {
        size_t slot;
        uint64_t h = hashSwap(nullptr, 0);
        g.find(h, nullptr, 0, slot);
        g.add(root, h, nullptr, 0, slot);
    }

    bool truncated = false;
    std::vector<std::vector<uint32_t>> frontier(depth + 1);
    frontier[0].push_back(0);
    uint32_t counter = 0;

    // Level by level (by slide count). Only the retained representative of each swap is
    // expanded; a cheaper-but-longer path found later replaces the stored one and is
    // re-expanded at its (new) level.
    for (int len = 0; len < depth && !truncated; ++len) {
        for (size_t k = 0; k < frontier[len].size() && !truncated; ++k) {
            const uint32_t id = frontier[len][k];
            const MoveRec cur = g.mv[id];  // copy: g.mv may reallocate below
            if (cur.len != len) continue;  // replaced by a longer path meanwhile
            if (g.expLen[id] == len && g.expCost[id] == cur.cost) continue;  // already expanded
            g.expLen[id] = static_cast<uint8_t>(len);
            g.expCost[id] = cur.cost;

            Pair base[kMaxPairs];
            const int baseN = cur.pairCnt;
            std::memcpy(base, g.pool.data() + cur.pairOff, sizeof(Pair) * baseN);
            const int lastDir = len > 0 ? static_cast<int>((cur.path >> (2 * (len - 1))) & 3u) : -1;

            for (int d = 0; d < NUM_DIRS; ++d) {
                if (lastDir >= 0 && d == oppositeDir(lastDir)) continue;  // immediate reversal
                Pair tmp[kMaxPairs];
                int n = baseN;
                std::memcpy(tmp, base, sizeof(Pair) * baseN);
                const int er = cur.endDr, ec = cur.endDc;
                const int nr = er + EMPTY_STEP[d].dr, nc = ec + EMPTY_STEP[d].dc;
                // Token currently sitting at the cell the empty cell moves into.
                int tr = nr, tc = nc;
                int i = findDst(tmp, n, nr, nc);
                if (i >= 0) { tr = tmp[i].sr; tc = tmp[i].sc; }
                setCell(tmp, n, er, ec, tr, tc);  // old empty cell receives that tile
                setCell(tmp, n, nr, nc, 0, 0);    // new empty cell receives the empty token
                std::sort(tmp, tmp + n, [](const Pair& a, const Pair& b) {
                    return a.dr != b.dr ? a.dr < b.dr : a.dc < b.dc;
                });

                MoveRec child{};
                child.cost = cur.cost + costs.c[d];
                child.path = cur.path | (static_cast<uint64_t>(d) << (2 * len));
                child.len = static_cast<uint8_t>(len + 1);
                child.mUp = static_cast<uint8_t>(std::max<int>(cur.mUp, -nr));
                child.mDown = static_cast<uint8_t>(std::max<int>(cur.mDown, nr));
                child.mLeft = static_cast<uint8_t>(std::max<int>(cur.mLeft, -nc));
                child.mRight = static_cast<uint8_t>(std::max<int>(cur.mRight, nc));
                child.endDr = static_cast<int8_t>(nr);
                child.endDc = static_cast<int8_t>(nc);

                const uint64_t h = hashSwap(tmp, n);
                size_t slot;
                const uint32_t found = g.find(h, tmp, n, slot);
                if (found == kNoSlot) {
                    if (g.mv.size() >= kMaxMoves) { truncated = true; break; }
                    uint32_t nid = g.add(child, h, tmp, n, slot);
                    frontier[len + 1].push_back(nid);
                } else {
                    MoveRec& old = g.mv[found];
                    if (child.cost < old.cost || (child.cost == old.cost && child.len < old.len)) {
                        const uint32_t po = old.pairOff;
                        const uint8_t pc = old.pairCnt;
                        old = child;
                        old.pairOff = po;
                        old.pairCnt = pc;
                        frontier[len + 1].push_back(found);  // re-expand from its new level
                    }
                }
            }
            if ((++counter & 255u) == 0 && Clock::now() >= deadline) truncated = true;
        }
    }

    // Final layout: sorted by len (stable), pairs re-packed in the same order.
    MoveTable t;
    t.truncated = truncated;
    int maxLen = 0;
    for (const MoveRec& m : g.mv) maxLen = std::max<int>(maxLen, m.len);
    t.maxLen = maxLen;
    std::vector<uint32_t> cnt(maxLen + 2, 0);
    for (const MoveRec& m : g.mv) ++cnt[m.len + 1];
    for (int l = 0; l <= maxLen; ++l) cnt[l + 1] += cnt[l];
    t.lenStart = cnt;  // lenStart[l] = number of moves with len < l
    std::vector<uint32_t> fill(cnt.begin(), cnt.end() - 1);
    t.moves.resize(g.mv.size());
    t.pairs.reserve(g.pool.size());
    std::vector<uint32_t> order(g.mv.size());
    for (uint32_t id = 0; id < g.mv.size(); ++id) order[fill[g.mv[id].len]++] = id;
    for (uint32_t k = 0; k < order.size(); ++k) {
        MoveRec m = g.mv[order[k]];
        const uint32_t off = static_cast<uint32_t>(t.pairs.size());
        t.pairs.insert(t.pairs.end(), g.pool.begin() + m.pairOff, g.pool.begin() + m.pairOff + m.pairCnt);
        m.pairOff = off;
        t.moves[k] = m;
    }
    return t;
}

}  // namespace sp
