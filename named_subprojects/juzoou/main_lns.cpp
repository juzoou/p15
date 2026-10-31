// main_lns.cpp -- точка входа в стиле вашего main.cpp.
// usage: p15_lns input.txt out.txt [time_seconds=30] [-v]
// Вход: ваш формат (parser.h: 4 цены, rows cols, клетки) либо формат презентации (DATA/SIZE/COST).
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "lns.h"
#include "parser.h"

// запасной разбор формата презентации (DATA / SIZE / COST, '=' -- пустая клетка)
static bool parse_data_format(const std::string& path, PuzzleInput& p) {
    std::ifstream f(path);
    if (!f) return false;
    std::string text, line;
    while (std::getline(f, line)) {
        auto c = line.find("--");
        if (c != std::string::npos) line.resize(c);
        for (char& ch : line) if (ch == '/') ch = ' ';
        text += line + "\n";
    }
    std::istringstream in(text);
    std::string tok;
    bool haveData = false;
    p.weights = {1, 1, 1, 1};
    while (in >> tok) {
        for (auto& ch : tok) ch = (char)std::toupper((unsigned char)ch);
        if (tok == "DATA") {
            in >> tok;
            if (tok != "SIZE") return false;
            long r, c;
            if (!(in >> r >> c) || r < 1 || c < 1 || r * c > 10000) return false;
            p.rows = (uint32_t)r; p.cols = (uint32_t)c;
            p.cells.clear();
            std::streampos pos;
            while (p.cells.size() < (size_t)(r * c) && in >> tok) {
                if (tok == "=") p.cells.push_back(0);
                else p.cells.push_back((uint16_t)std::atoi(tok.c_str()));
            }
            haveData = p.cells.size() == (size_t)(r * c);
        } else if (tok == "COST") {
            for (auto& w : p.weights) { long v; if (!(in >> v)) return false; w = (uint32_t)v; }
        } else if (tok == "ORDER") {
            break;
        }
    }
    return haveData;
}

static void write_output(const char* path, const PuzzleInput& p, const std::vector<LnsMove>& moves) {
    std::string text = "DATA\nSIZE " + std::to_string(p.rows) + ' ' + std::to_string(p.cols) + '\n';
    for (uint32_t r = 0; r < p.rows; ++r) {
        for (uint32_t c = 0; c < p.cols; ++c) {
            if (c) text += ' ';
            const uint16_t v = p.cells[r * p.cols + c];
            if (v == 0) text += '=';
            else text += std::to_string(v);
        }
        text += '\n';
    }
    text += "/\nCOST\n";
    text += std::to_string(p.weights[0]) + ' ' + std::to_string(p.weights[1]) + ' ' +
            std::to_string(p.weights[2]) + ' ' + std::to_string(p.weights[3]) + " /\nORDER\n";
    for (const LnsMove& m : moves) {
        text += std::to_string(m.tile);
        text += ' ';
        text += m.dir;
        text += '\n';
    }
    text += "/\n";
    std::ofstream(path, std::ios::binary) << text;
}

int main(int argc, const char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " input_file.txt out_file.txt [time_seconds] [-v]\n";
        return 1;
    }
    double limit = 30;
    int verbose = 0;
    for (int i = 3; i < argc; i++) {
        std::string s = argv[i];
        if (s == "-v") verbose = 1;
        else limit = std::atof(argv[i]);
    }
    PuzzleInput puzzle;
    if (auto parsed = parse_file(argv[1])) puzzle = *parsed;
    else if (!parse_data_format(argv[1], puzzle)) {
        std::cerr << "Input error\n";
        return 1;
    }
    {
        std::ofstream t(argv[2], std::ios::app | std::ios::binary);
        if (!t) { std::cerr << "Cannot write " << argv[2] << "\n"; return 3; }
    }
    LnsResult r = solve_lns(puzzle.rows, puzzle.cols, puzzle.weights, puzzle.cells, limit, verbose);
    write_output(argv[2], puzzle, r.moves);
    if (r.unsolvable) std::cerr << "Goal unreachable: best reachable arrangement reported\n";
    std::cerr << "Done (moves=" << r.moves.size() << ", P=" << r.placed << ", F=" << r.F << ")\n";
    std::cout << r.F << "\n";
    return 0;
}
