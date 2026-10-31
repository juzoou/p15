// main_ida.cpp -- точка входа решателя "ida".
// Использование: p15_ida <задача> <файл решения> [--time T] [-v] ...
#include "solvers.hpp"

int main(int argc, char** argv) {
    return p15::runSolverMain(argc, argv, "ida", 30, p15::solveIda);
}
