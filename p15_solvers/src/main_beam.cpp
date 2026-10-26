// main_beam.cpp -- точка входа решателя "beam".
// Использование: p15_beam <задача> <файл решения> [--time T] [-v] ...
#include "solvers.hpp"

int main(int argc, char** argv) {
    return p15::runSolverMain(argc, argv, "beam", 30, p15::solveBeam);
}
