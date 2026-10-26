CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic
INCLUDES = -Iinclude
LDLIBS =
ifeq ($(OS),Windows_NT)
LDLIBS += -lshell32
endif
SOURCES = src/main.cpp src/parser.cpp src/validation.cpp src/algorithms_exact.cpp src/algorithms_advanced.cpp src/solver_rowbyrow.cpp src/solver_columnbycolumn.cpp

.PHONY: all clean test
all: pyatnashki_cpp

pyatnashki_cpp: $(SOURCES) $(wildcard include/pyatnashki/*.hpp)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SOURCES) -o $@ $(LDLIBS)

test: pyatnashki_cpp
	python3 tests/test_rowbyrow.py
	python3 tests/test_columnbycolumn.py
	python3 tests/test_generator.py
	python3 tests/test_weighted_astar.py
	python3 tests/test_advanced_algorithms.py

clean:
	rm -f pyatnashki_cpp
