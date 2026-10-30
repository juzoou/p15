#!/bin/sh
cd "$(dirname "$0")"
g++ -O2 -std=c++17 -Wall -Wextra main_lns.cpp lns.cpp parser.cpp -o p15_lns
