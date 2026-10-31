#include "pyatnashki/types.hpp"

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

        SearchResult result;
        if (algorithm == "ROWBYROW") result = row_by_row(puzzle);
        else if (algorithm == "COLUMNBYCOLUMN") result = column_by_column(puzzle);
        else if (algorithm == "ASTAR") result = best_first(puzzle, max_expanded, true);
        else if (algorithm == "WASTAR") result = weighted_a_star(puzzle, max_expanded, weight);
        else if (algorithm == "LINEARCONFLICT") result = linear_conflict_astar(puzzle, max_expanded);
        else if (algorithm == "RBFS") result = rbfs(puzzle, max_expanded);
        else if (algorithm == "PDB") result = pattern_database(puzzle, max_expanded);
        else if (algorithm == "ANNEALING") result = simulated_annealing(puzzle, max_expanded);
        else if (algorithm == "DIJKSTRA") result = best_first(puzzle, max_expanded, false);
        else if (algorithm == "BIDIJKSTRA") result = bidirectional_dijkstra(puzzle, max_expanded);
        else if (algorithm == "IDASTAR") result = ida_star(puzzle, max_expanded);
        else if (algorithm == "01BFS") result = zero_one_bfs(puzzle, max_expanded);
        else if (algorithm == "DIAL") result = dial(puzzle, max_expanded);
        else throw std::runtime_error("unknown algorithm: " + algorithm);

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
