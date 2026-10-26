#!/bin/sh
# Сборка всех решателей без CMake (Linux / macOS / MinGW). Результат -- в bin/.
set -e
cd "$(dirname "$0")"
CXX=${CXX:-g++}
mkdir -p bin build
SRC="common io route exact reduce pdb ida greedy beam best"
OBJ=""
for s in $SRC; do
  $CXX -O2 -std=c++17 -Wall -Wextra -c src/$s.cpp -o build/$s.o
  OBJ="$OBJ build/$s.o"
done
for a in exact ida reduce greedy beam best; do
  $CXX -O2 -std=c++17 src/main_$a.cpp $OBJ -o bin/p15_$a
done
echo "built: $(ls bin)"
