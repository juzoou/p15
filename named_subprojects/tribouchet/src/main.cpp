// Sliding puzzle (generalised 15-puzzle) solver.
// Pipeline: parse -> solvability -> move table (stage 1) -> beam search with tabu (stage 2)
//           -> constructive fallback on a stall / deadline -> verify -> write.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "beam.hpp"
#include "common.hpp"
#include "fallback.hpp"
#include "heuristic.hpp"
#include "move_table.hpp"
#include "peephole.hpp"
#include "problem.hpp"

using namespace sp;

namespace {

struct Options {
    std::string taskFile, solutionFile;
    int depth = 10;         // precomputation depth (slides per move); tune by hand against RAM/time
    double weight = 1.0;    // heuristic weight in g + W*h
    int stall = 50;         // iterations without progress that count as a stall
    int beam = 0;           // 0 = automatic (see autoBeamWidth)
    double timeSec = 10.0;  // total time budget of the run (a guess; set by hand)
};

// Output file bookkeeping: if the run fails after we created the file, remove it again so that
// failures leave no output behind.
std::string gSolutionPath;
bool gCreatedByUs = false;

int fail(const std::string& msg) {
    std::fprintf(stderr, "error: %s\n", msg.c_str());
    if (gCreatedByUs && !gSolutionPath.empty()) std::remove(gSolutionPath.c_str());
    return 1;
}

// Strips surrounding whitespace and quote characters (some launchers pass them through literally).
std::string cleanValue(const std::string& v) {
    size_t b = 0, e = v.size();
    auto junk = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '"' || c == '\''; };
    while (b < e && junk(v[b])) ++b;
    while (e > b && junk(v[e - 1])) --e;
    return v.substr(b, e - b);
}

bool parseArgs(int argc, char** argv, Options& o, std::string& err) {
    std::vector<std::string> pos;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a.size() > 2 && a[0] == '-' && a[1] == '-') {
            std::string v;
            const size_t eq = a.find('=');  // also accept --option=value
            if (eq != std::string::npos) {
                v = a.substr(eq + 1);
                a = a.substr(0, eq);
            } else {
                if (i + 1 >= argc) { err = "missing value for " + a; return false; }
                v = argv[++i];
            }
            v = cleanValue(v);
            std::string num = v;
            for (char& c : num)
                if (c == ',') c = '.';  // decimal comma
            char* end = nullptr;
            if (a == "--depth") {
                long x = std::strtol(num.c_str(), &end, 10);
                if (num.empty() || *end || x < 1) { err = "bad --depth value '" + v + "'"; return false; }
                o.depth = static_cast<int>(x > kMaxDepth ? kMaxDepth : x);
            } else if (a == "--weight") {
                double x = std::strtod(num.c_str(), &end);
                if (num.empty() || *end || x < 0) { err = "bad --weight value '" + v + "'"; return false; }
                o.weight = x;
            } else if (a == "--stall") {
                long x = std::strtol(num.c_str(), &end, 10);
                if (num.empty() || *end || x < 1) { err = "bad --stall value '" + v + "'"; return false; }
                o.stall = static_cast<int>(x);
            } else if (a == "--beam") {
                long x = std::strtol(num.c_str(), &end, 10);
                if (num.empty() || *end || x < 1) { err = "bad --beam value '" + v + "'"; return false; }
                o.beam = static_cast<int>(x);
            } else if (a == "--time") {
                double x = std::strtod(num.c_str(), &end);
                if (num.empty() || *end || x < 0) { err = "bad --time value '" + v + "'"; return false; }
                o.timeSec = x;
            } else {
                err = "unknown option " + a;
                return false;
            }
        } else {
            pos.push_back(a);
        }
    }
    if (pos.size() != 2) { err = "usage: solver <task_file> <solution_file> [--depth N] [--weight W] [--stall K] [--beam B] [--time T]"; return false; }
    o.taskFile = pos[0];
    o.solutionFile = pos[1];
    return true;
}

// Automatic beam width from the cell count (guesses from the spec, not tuned).
int autoBeamWidth(int cells) {
    if (cells <= 400) return 4;
    if (cells <= 2500) return 2;
    return 1;
}

int64_t slidesCost(const Costs& c, const std::vector<uint8_t>& slides) {
    int64_t total = 0;
    for (uint8_t d : slides) total += c.c[d];
    return total;
}

bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool writeFile(const std::string& path, const std::string& data) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
    f.flush();
    return static_cast<bool>(f);
}

}  // namespace

int main(int argc, char** argv) {
    const Clock::time_point t0 = Clock::now();
    std::ios::sync_with_stdio(false);

    Options opt;
    std::string err;
    if (!parseArgs(argc, argv, opt, err)) return fail(err);

    // The solution file must be writable before any solving starts. Opening in append mode
    // creates it without truncating an existing file.
    gSolutionPath = opt.solutionFile;
    {
        std::ifstream probe(opt.solutionFile, std::ios::binary);
        const bool existed = static_cast<bool>(probe);
        probe.close();
        std::ofstream f(opt.solutionFile, std::ios::binary | std::ios::app);
        if (!f) return fail("cannot open solution file for writing: " + opt.solutionFile);
        gCreatedByUs = !existed;
    }

    std::string text;
    if (!readFile(opt.taskFile, text)) return fail("cannot read task file: " + opt.taskFile);
    Problem prob;
    bool sawOrder = false;
    if (!parseProblem(text, prob, err, sawOrder)) return fail(err);
    text.clear();
    text.shrink_to_fit();

    const int n = prob.n();
    const int64_t P = n - 1;  // all tiles are in place in every valid solution

    if (!isSolvable(prob)) return fail("the board is unsolvable");

    std::vector<uint8_t> slides;  // tile directions of the whole solution

    if (!isSolved(prob.cells)) {
        const auto budget = std::chrono::duration<double>(opt.timeSec);
        const Clock::time_point precompDeadline = t0 + std::chrono::duration_cast<Clock::duration>(budget * 0.30);
        const Clock::time_point beamDeadline = t0 + std::chrono::duration_cast<Clock::duration>(budget * 0.70);

        // Stage 1: moves and swaps (stops early once 30% of the budget is used).
        MoveTable table = buildMoveTable(prob.costs, opt.depth, precompDeadline);

        // Stage 2: beam search with tabu; on a stall or at the 70% deadline, hand over to the fallback.
        Scorer scorer(prob);
        BeamOptions bo;
        bo.width = opt.beam > 0 ? opt.beam : autoBeamWidth(n);
        bo.weight = opt.weight;
        bo.stallLimit = opt.stall;
        bo.deadline = beamDeadline;
        const double tTable = secondsSince(t0);
        BeamOutcome res = runBeam(prob, table, scorer, bo);
        if (std::getenv("SP_VERBOSE"))
            std::fprintf(stderr, "[sp] moves=%zu maxLen=%d table@%.3fs beam: width=%d iters=%ld stop=%s h %lld -> %lld slides=%zu @%.3fs\n",
                         table.moves.size(), table.maxLen, tTable, bo.width, res.iterations, res.stopReason,
                         (long long)res.startH, (long long)res.endH, res.slides.size(), secondsSince(t0));
        slides = res.slides;

        // Candidate A: the beam result, completed by the constructive fallback when it stalled.
        double fbDuration = 0.0;
        if (!res.solved) {
            const double tFb0 = secondsSince(t0);
            ConstructiveSolver fb(prob.rows, prob.cols, prob.costs, res.board, res.er, res.ec);
            if (!fb.run()) return fail("constructive fallback failed: " + fb.error());
            slides.insert(slides.end(), fb.slides().begin(), fb.slides().end());
            fbDuration = secondsSince(t0) - tFb0;
        } else {
            fbDuration = n * 5e-5;  // conservative estimate of one fallback run (100x100 ~ 0.3 s)
        }

        // Candidate B (safety net, an addition to the spec): the plain constructive method from the
        // initial board. A beam prefix can cost more than that, so if the time budget allows one more
        // fallback run, keep whichever complete solution is cheaper.
        if (!res.slides.empty() && secondsSince(t0) + 1.5 * fbDuration + 0.05 <= opt.timeSec) {
            int er0 = 0, ec0 = 0;
            for (int k = 0; k < n; ++k)
                if (prob.cells[k] == 0) { er0 = k / prob.cols; ec0 = k % prob.cols; }
            ConstructiveSolver alt(prob.rows, prob.cols, prob.costs, prob.cells, er0, ec0);
            if (alt.run() && slidesCost(prob.costs, alt.slides()) < slidesCost(prob.costs, slides)) {
                if (std::getenv("SP_VERBOSE")) std::fprintf(stderr, "[sp] safety net: plain fallback is cheaper\n");
                slides = alt.slides();
            }
        }

        // Peephole pass (addition to the spec): shorten stretches of the final solution using the
        // move table; the board effect of every replacement is identical.
        const Clock::time_point peepDeadline = t0 + std::chrono::duration_cast<Clock::duration>(budget * 0.90);
        const int64_t before = slidesCost(prob.costs, slides);
        const size_t repl = optimizeSlides(prob, table, slides, peepDeadline);
        if (std::getenv("SP_VERBOSE"))
            std::fprintf(stderr, "[sp] peephole: %zu replacements, cost %lld -> %lld @%.3fs\n", repl,
                         (long long)before, (long long)slidesCost(prob.costs, slides), secondsSince(t0));
    }

    // Replay from the original board: never write a sequence that does not solve it.
    std::string out;
    int64_t totalCost = 0;
    if (!buildSolutionText(prob, slides, out, totalCost, err)) return fail(err);
    if (!writeFile(opt.solutionFile, out)) return fail("cannot write solution file");

    const int64_t F = 100 * P - totalCost;
    std::cout << F << "\n";
    std::cout.flush();
    return 0;
}
