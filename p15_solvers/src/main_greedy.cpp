// main_greedy.cpp -- точка входа решателя "greedy".
// Использование: p15_greedy <задача> <файл решения> [--time T] [-v] ...
#include "solvers.hpp"

int main(int argc, char** argv) {
    return p15::runSolverMain(argc, argv, "greedy", 30, p15::solveGreedy);
}
