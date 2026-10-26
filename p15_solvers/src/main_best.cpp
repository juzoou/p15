// main_best.cpp -- точка входа решателя "best".
// Использование: p15_best <задача> <файл решения> [--time T] [-v] ...
#include "solvers.hpp"

int main(int argc, char** argv) {
    return p15::runSolverMain(argc, argv, "best", 60, p15::solveBest);
}
