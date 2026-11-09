#include "peephole.hpp"

#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace sp {

size_t optimizeSlides(const Problem& p, const MoveTable& table, std::vector<uint8_t>& slides,
                      Clock::time_point deadline) {
    if (table.maxLen < 2 || table.moves.size() < 2 || slides.size() < 2) return 0;

    std::unordered_map<uint64_t, uint32_t> index;
    index.reserve(table.moves.size() * 2);
    for (uint32_t k = 0; k < table.moves.size(); ++k) {
        const MoveRec& m = table.moves[k];
        index.emplace(hashSwap(table.pairs.data() + m.pairOff, m.pairCnt), k);
    }

    int er0 = 0, ec0 = 0;
    for (int k = 0; k < p.n(); ++k)
        if (p.cells[k] == 0) { er0 = k / p.cols; ec0 = k % p.cols; }

    const int Lmax = std::min(table.maxLen, 12);  // window cost grows ~L^2
    std::vector<uint8_t> out;
    out.reserve(slides.size());
    std::vector<int32_t> posR{er0}, posC{ec0};  // empty cell before slide k (and after the last one)

    auto push = [&](int d) {
        out.push_back(static_cast<uint8_t>(d));
        posR.push_back(posR.back() + EMPTY_STEP[d].dr);
        posC.push_back(posC.back() + EMPTY_STEP[d].dc);
    };
    auto pop = [&](size_t L) {
        out.resize(out.size() - L);
        posR.resize(posR.size() - L);
        posC.resize(posC.size() - L);
    };

    size_t replaced = 0;
    // Looks at the windows that end at the current tail, longest first; applies the first gain.
    auto reduceOnce = [&]() -> bool {
        const int maxL = static_cast<int>(std::min<size_t>(Lmax, out.size()));
        for (int L = maxL; L >= 2; --L) {
            const size_t start = out.size() - L;
            SwapBuilder sb;
            int64_t windowCost = 0;
            for (size_t k = start; k < out.size(); ++k) {
                sb.step(out[k]);
                windowCost += p.costs.c[out[k]];
            }
            Pair canon[kMaxDepth + 4];
            sb.sorted(canon);
            auto it = index.find(hashSwap(canon, sb.n));
            if (it == index.end()) continue;
            const MoveRec& m = table.moves[it->second];
            if (m.pairCnt != sb.n) continue;
            if (sb.n > 0 && std::memcmp(table.pairs.data() + m.pairOff, canon, sizeof(Pair) * sb.n) != 0) continue;
            if (!(m.cost < windowCost || (m.cost == windowCost && m.len < L))) continue;
            if (!table.legalAt(m, posR[start], posC[start], p.rows, p.cols)) continue;
            pop(static_cast<size_t>(L));
            for (int i = 0; i < m.len; ++i) push(static_cast<int>((m.path >> (2 * i)) & 3u));
            ++replaced;
            return true;
        }
        return false;
    };

    bool timeUp = false;
    for (size_t i = 0; i < slides.size(); ++i) {
        push(slides[i]);
        if (timeUp) continue;
        while (reduceOnce()) {}
        if ((i & 1023u) == 0 && Clock::now() >= deadline) timeUp = true;
    }
    slides.swap(out);
    return replaced;
}

}  // namespace sp
