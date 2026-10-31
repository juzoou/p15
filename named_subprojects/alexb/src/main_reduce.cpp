// main_reduce.cpp -- точка входа решателя "reduce".
// Использование: p15_reduce <задача> <файл решения> [--time T] [-v] ...
#include "solvers.hpp"

int main(int argc, char** argv) {
    return p15::runSolverMain(argc, argv, "reduce", 10, p15::solveReduce);
}
