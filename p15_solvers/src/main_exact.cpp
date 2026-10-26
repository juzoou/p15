// main_exact.cpp -- точка входа решателя "exact".
// Использование: p15_exact <задача> <файл решения> [--time T] [-v] ...
#include "solvers.hpp"

int main(int argc, char** argv) {
    return p15::runSolverMain(argc, argv, "exact", 0, p15::solveExact);
}
