#include "pyatnashki/types.hpp"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

using namespace pyatnashki;

int run_main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: pyatnashki_cpp INPUT OUTPUT"
                     " [--algorithm auto|rowbyrow|columnbycolumn|divideconquer|astar|linearconflict|wastar|rbfs|pdb|annealing|dijkstra|bidijkstra|idastar|01bfs|dial]"
                     " [--weight W] [--max-expanded N] [--score-only]\n";
        return 2;
    }
    std::string algorithm = "AUTO";
    std::vector<std::string> paths;
    bool score_only = false;
    std::uint64_t max_expanded = 1000000;
    double weight = 1.8;
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string option = argv[i];
            if (option == "--algorithm" && i + 1 < argc) {
                algorithm = upper(argv[++i]);
            } else if (option == "--weight" && i + 1 < argc) {
                std::size_t used = 0;
                const std::string value = argv[++i];
                weight = std::stod(value, &used);
                if (used != value.size() || !(weight >= 1.0) || !std::isfinite(weight))
                    throw std::runtime_error("--weight must be finite and >= 1.0");
            } else if (option == "--max-expanded" && i + 1 < argc) {
                const auto limit = parse_integer(argv[++i], "max-expanded");
                if (limit < 1) throw std::runtime_error("--max-expanded must be positive");
                max_expanded = static_cast<std::uint64_t>(limit);
            } else if (option == "--score-only") {
                score_only = true;
            } else if (option.rfind("--", 0) != 0) {
                paths.push_back(option);
            } else {
                throw std::runtime_error("unknown or incomplete option: " + option);
            }
        }

        if (paths.size() != 2) throw std::runtime_error("provide INPUT and OUTPUT paths");
        const auto input_path = std::filesystem::weakly_canonical(std::filesystem::u8path(paths[0]));
        const auto output_path = std::filesystem::weakly_canonical(std::filesystem::u8path(paths[1]));
        if (input_path == output_path ||
            (std::filesystem::exists(output_path) && std::filesystem::equivalent(input_path, output_path)))
            throw std::runtime_error("INPUT and OUTPUT must be different files");
        const Puzzle puzzle = parse_puzzle(paths[0]);
        // Check writability before searching, without truncating an existing result.
        {
            std::ofstream probe(std::filesystem::u8path(paths[1]), std::ios::app);
            if (!probe) throw std::runtime_error("cannot open output file: " + paths[1]);
        }
        if (puzzle.has_order) {
            const auto checked = check_order(puzzle);
            const Cost score = score_for(static_cast<Cost>(checked.placed), checked.total_cost);
            const std::string summary = "CHECK: VALID; moves=" + std::to_string(puzzle.order.size()) +
                "; placed=" + std::to_string(checked.placed) + "; cost=" +
                std::to_string(checked.total_cost) + "; F=" + std::to_string(score) +
                "; solved=" + (checked.final_board == puzzle.goal() ? "true" : "false");
            write_text(paths[1], render_output(puzzle, puzzle.order, summary));
            if (score_only) std::cout << score << '\n';
            else std::cout << summary << "\nFinal board:\n"
                           << format_board(checked.final_board, puzzle.rows, puzzle.cols) << '\n';
            return 0;
        }
        if (!solvable(puzzle)) throw std::runtime_error("puzzle is unreachable (tile order or permutation parity)");

        if (algorithm == "DIVIDECONQUER") algorithm = puzzle.rows <= puzzle.cols ? "ROWBYROW" : "COLUMNBYCOLUMN";
        if (algorithm == "AUTO") {
            // Exact search is useful for small boards; switch to the constructive
            // polynomial solver before the factorial state space becomes prohibitive.
            if (puzzle.start.size() <= 16) algorithm = "ASTAR";
            else algorithm = puzzle.rows <= puzzle.cols ? "ROWBYROW" : "COLUMNBYCOLUMN";
        }

        const auto search = [&](const Puzzle& board) -> SearchResult {
            if (algorithm == "ASTAR") return best_first(board, max_expanded, true);
            if (algorithm == "WASTAR") return weighted_a_star(board, max_expanded, weight);
            if (algorithm == "LINEARCONFLICT") return linear_conflict_astar(board, max_expanded);
            if (algorithm == "RBFS") return rbfs(board, max_expanded);
            if (algorithm == "PDB") return pattern_database(board, max_expanded);
            if (algorithm == "ANNEALING") return simulated_annealing(board, max_expanded);
            if (algorithm == "DIJKSTRA") return best_first(board, max_expanded, false);
            if (algorithm == "BIDIJKSTRA") return bidirectional_dijkstra(board, max_expanded);
            if (algorithm == "IDASTAR") return ida_star(board, max_expanded);
            if (algorithm == "01BFS") return zero_one_bfs(board, max_expanded);
            if (algorithm == "DIAL") return dial(board, max_expanded);
            throw std::runtime_error("unknown algorithm: " + algorithm);
        };
        SearchResult result;
        std::string reduction;
        std::string tail_algorithm;
        if (algorithm == "ROWBYROW") result = row_by_row(puzzle);
        else if (algorithm == "COLUMNBYCOLUMN") result = column_by_column(puzzle);
        else if (puzzle.start.size() <= 16) result = search(puzzle);
        else {
            // Validate the selector even if a solved or one-dimensional board needs no tail.
            const std::array<std::string, 11> searches{
                "ASTAR", "WASTAR", "LINEARCONFLICT", "RBFS", "PDB", "ANNEALING",
                "DIJKSTRA", "BIDIJKSTRA", "IDASTAR", "01BFS", "DIAL"};
            if (std::find(searches.begin(), searches.end(), algorithm) == searches.end())
                throw std::runtime_error("unknown algorithm: " + algorithm);
            const bool rows_first = puzzle.rows <= puzzle.cols;
            reduction = rows_first ? "ROWBYROW" : "COLUMNBYCOLUMN";
            const auto solve_tail = [&](const Puzzle& tail) -> SearchResult {
                const auto [min_cost, max_cost] = std::minmax_element(tail.costs.begin(), tail.costs.end());
                const bool compatible = (algorithm != "PDB" || (*min_cost == 1 && *max_cost == 1)) &&
                    (algorithm != "01BFS" || *max_cost <= 1) &&
                    (algorithm != "DIAL" || *max_cost <= 100000);
                if (compatible) {
                    try {
                        auto solved = search(tail);
                        tail_algorithm = algorithm;
                        return solved;
                    } catch (const SearchLimitExceeded&) {
                        // At most 360 reachable states remain. Finish with bounded Dijkstra.
                    }
                }
                tail_algorithm = "DIJKSTRA";
                return best_first(tail, 360, false);
            };
            result = rows_first ? row_by_row(puzzle, solve_tail) : column_by_column(puzzle, solve_tail);
        }

        Puzzle verification = puzzle;
        verification.order = result.moves;
        const auto checked = check_order(verification);
        if (checked.final_board != puzzle.goal() || checked.total_cost != result.total_cost)
            throw std::runtime_error("internal error: solver returned an invalid solution or cost");
        const Cost score = score_for(static_cast<Cost>(puzzle.start.size() - 1), result.total_cost);
        std::ostringstream summary;
        summary << "RESULT: SOLVED; algorithm=" << algorithm << "; moves=" << result.moves.size() << "; placed="
                << puzzle.start.size() - 1 << "; cost=" << result.total_cost << "; F=" << score
                << (algorithm == "WASTAR" ? "; weight=" + std::to_string(weight) : "")
                << (reduction.empty() ? "" : "; reduction=" + reduction)
                << (tail_algorithm.empty() ? "" : "; tail=" + tail_algorithm)
                << "; expanded=" << result.expanded << "; seconds=" << result.seconds;
        write_text(paths[1], render_output(puzzle, result.moves, summary.str()));
        if (score_only) std::cout << score << '\n';
        else std::cout << summary.str() << "\nResult written to " << paths[1] << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        return 2;
    }
}

int main(int argc, char** argv) {
#ifdef _WIN32
    // The Windows narrow argv can corrupt Cyrillic paths before filesystem sees them.
    int count = 0;
    auto wide = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!wide) { std::cerr << "ERROR: cannot read command line\n"; return 2; }
    std::vector<std::string> utf8;
    utf8.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const int size = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
        std::string value(static_cast<std::size_t>(size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, value.data(), size, nullptr, nullptr);
        value.pop_back();
        utf8.push_back(std::move(value));
    }
    LocalFree(wide);
    std::vector<char*> arguments;
    for (auto& value : utf8) arguments.push_back(value.data());
    (void)argc; (void)argv;
    return run_main(count, arguments.data());
#else
    return run_main(argc, argv);
#endif
}
