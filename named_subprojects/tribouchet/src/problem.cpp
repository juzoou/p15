#include "problem.hpp"

#include <cctype>
#include <cstdio>

namespace sp {

namespace {

constexpr int64_t kMaxCost = 1000000000;  // keeps all cost sums comfortably inside int64
constexpr int kMaxCells = 65535;          // cells are stored as uint16_t

// Splits on whitespace (\n and \r\n alike); '/' and '=' are tokens of their own;
// everything from "--" to the end of the line is a comment.
std::vector<std::string> tokenize(const std::string& s) {
    std::vector<std::string> toks;
    std::string cur;
    auto flush = [&]() {
        if (!cur.empty()) {
            toks.push_back(cur);
            cur.clear();
        }
    };
    bool inComment = false;  // "--" starts a comment that runs to the end of the line
    for (size_t i = 0; i < s.size(); ++i) {
        const char ch = s[i];
        const unsigned char u = static_cast<unsigned char>(ch);
        if (inComment) {
            if (ch == '\n') inComment = false;
            continue;
        }
        if (ch == '-' && i + 1 < s.size() && s[i + 1] == '-') {
            flush();
            inComment = true;
            continue;
        }
        if (std::isspace(u)) {
            flush();
        } else if (ch == '/' || ch == '=') {
            flush();
            toks.push_back(std::string(1, ch));
        } else {
            cur.push_back(ch);
        }
    }
    flush();
    return toks;
}

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// Strict non-negative integer (digits only, at most 10 digits).
bool toInt(const std::string& s, int64_t& v) {
    if (s.empty() || s.size() > 10) return false;
    int64_t x = 0;
    for (char ch : s) {
        if (ch < '0' || ch > '9') return false;
        x = x * 10 + (ch - '0');
    }
    v = x;
    return true;
}

}  // namespace

bool parseProblem(const std::string& text, Problem& p, std::string& err, bool& sawOrder) {
    sawOrder = false;
    std::vector<std::string> t = tokenize(text);
    size_t i = 0;
    bool haveData = false, haveCost = false;
    p = Problem();

    while (i < t.size()) {
        std::string kw = upper(t[i++]);
        if (kw == "DATA") {
            if (haveData) { err = "duplicate DATA section"; return false; }
            if (i >= t.size() || upper(t[i]) != "SIZE") { err = "DATA: expected SIZE"; return false; }
            ++i;
            int64_t n = 0, m = 0;
            if (i + 1 >= t.size() || !toInt(t[i], n) || !toInt(t[i + 1], m)) {
                err = "DATA: bad SIZE line";
                return false;
            }
            i += 2;
            if (n < 1 || m < 1 || n * m > kMaxCells) { err = "DATA: unsupported board size"; return false; }
            p.rows = static_cast<int>(n);
            p.cols = static_cast<int>(m);
            const int total = p.rows * p.cols;
            p.cells.assign(total, 0);
            std::vector<uint8_t> used(total, 0);
            int empties = 0;
            for (int k = 0; k < total; ++k) {
                if (i >= t.size()) { err = "DATA: too few entries"; return false; }
                const std::string& e = t[i++];
                if (e == "=") {
                    p.cells[k] = 0;
                    ++empties;
                } else {
                    int64_t v = 0;
                    if (!toInt(e, v) || v < 1 || v >= total) { err = "DATA: bad tile '" + e + "'"; return false; }
                    if (used[v]) { err = "DATA: duplicate tile"; return false; }
                    used[v] = 1;
                    p.cells[k] = static_cast<uint16_t>(v);
                }
            }
            if (empties != 1) { err = "DATA: need exactly one empty cell"; return false; }
            if (i >= t.size() || t[i] != "/") { err = "DATA: missing terminating '/'"; return false; }
            ++i;
            haveData = true;
        } else if (kw == "COST") {
            if (haveCost) { err = "duplicate COST section"; return false; }
            int64_t v[NUM_DIRS];
            for (int d = 0; d < NUM_DIRS; ++d) {
                if (i >= t.size() || !toInt(t[i], v[d]) || v[d] > kMaxCost) {
                    err = "COST: expected four non-negative integers (U D L R)";
                    return false;
                }
                ++i;
            }
            if (i >= t.size() || t[i] != "/") { err = "COST: missing terminating '/'"; return false; }
            ++i;
            for (int d = 0; d < NUM_DIRS; ++d) p.costs.c[d] = v[d];
            haveCost = true;
        } else if (kw == "ORDER") {
            sawOrder = true;
            err = "ORDER section in the input is not supported";
            return false;
        } else {
            err = "unexpected token '" + t[i - 1] + "'";
            return false;
        }
    }
    if (!haveData) { err = "missing DATA section"; return false; }
    return true;
}

bool isSolved(const std::vector<uint16_t>& cells) {
    const size_t n = cells.size();
    for (size_t k = 0; k + 1 < n; ++k)
        if (cells[k] != k + 1) return false;
    return n == 0 || cells[n - 1] == 0;
}

bool isSolvable(const Problem& p) {
    if (isSolved(p.cells)) return true;
    if (p.rows == 1 || p.cols == 1) return false;  // tiles can never change order
    const int n = p.n();
    // Permutation: cell k holds value v (empty counts as tile n, whose target is the last cell).
    std::vector<int> perm(n);
    int emptyPos = 0;
    for (int k = 0; k < n; ++k) {
        int v = p.cells[k];
        if (v == 0) { v = n; emptyPos = k; }
        perm[k] = v - 1;
    }
    std::vector<uint8_t> seen(n, 0);
    int cycles = 0;
    for (int k = 0; k < n; ++k) {
        if (seen[k]) continue;
        ++cycles;
        for (int j = k; !seen[j]; j = perm[j]) seen[j] = 1;
    }
    const int permParity = (n - cycles) & 1;
    const int er = emptyPos / p.cols, ec = emptyPos % p.cols;
    const int dist = (p.rows - 1 - er) + (p.cols - 1 - ec);
    return permParity == (dist & 1);
}

bool buildSolutionText(const Problem& p, const std::vector<uint8_t>& slides, std::string& out,
                       int64_t& totalCost, std::string& err) {
    const int R = p.rows, C = p.cols;
    std::vector<uint16_t> b = p.cells;
    int er = 0, ec = 0;
    for (int k = 0; k < R * C; ++k)
        if (b[k] == 0) { er = k / C; ec = k % C; }

    out.clear();
    out.reserve(static_cast<size_t>(R) * C * 6 + slides.size() * 8 + 64);
    out += "DATA\nSIZE ";
    out += std::to_string(R);
    out += ' ';
    out += std::to_string(C);
    out += '\n';
    for (int r = 0; r < R; ++r) {
        for (int c = 0; c < C; ++c) {
            if (c) out += ' ';
            const uint16_t v = p.cells[r * C + c];
            if (v == 0) out += '='; else out += std::to_string(v);
        }
        out += '\n';
    }
    out += "/\nORDER\n";

    totalCost = 0;
    char buf[16];
    for (uint8_t d : slides) {
        const int nr = er + EMPTY_STEP[d].dr, nc = ec + EMPTY_STEP[d].dc;
        if (nr < 0 || nr >= R || nc < 0 || nc >= C) {
            err = "internal error: illegal slide in solution";
            return false;
        }
        const uint16_t tile = b[nr * C + nc];
        b[er * C + ec] = tile;
        b[nr * C + nc] = 0;
        er = nr;
        ec = nc;
        totalCost += p.costs.c[d];
        // "<tile> <dir>\n"
        int len = 0;
        char tmp[8];
        unsigned v = tile;
        do { tmp[len++] = static_cast<char>('0' + v % 10); v /= 10; } while (v);
        int w = 0;
        while (len) buf[w++] = tmp[--len];
        buf[w++] = ' ';
        buf[w++] = DIR_CHAR[d];
        buf[w++] = '\n';
        out.append(buf, w);
    }
    out += "/\n";
    if (!isSolved(b)) {
        err = "internal error: final board is not the target";
        return false;
    }
    return true;
}

}  // namespace sp
