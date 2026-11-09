# Sliding puzzle solver (N x M, C++17)

Solves the rectangular sliding puzzle to the fixed target (tiles `1..N*M-1` ascending, empty cell bottom-right)
while keeping the total directional cost low. Single binary, standard library only.

## Build

Python 3 (standard library only) drives the build; no CMake/Make needed.

```
# Linux
python3 build.py            # -> ./solver

# Windows (MinGW g++, clang++, or MSVC)
python build.py             # -> solver.exe
```

* Compiler order: `$CXX` if set, else `g++`, `clang++`, `cl.exe`. On Windows, if `cl.exe` is not on `PATH` the script
  tries to find Visual Studio via `vswhere.exe` and load its x64 environment; if that fails it asks you to run it from a
  *Developer Command Prompt for VS*.
* Flags: `-std=c++17 -O2` (g++/clang++), `/std:c++17 /O2 /EHsc` (MSVC). MSVC object files go to `build/`.
* `--debug`: no optimisation, debug info, and `-DSP_VERIFY` (the beam search re-checks every incremental score,
  hash and goal count against a full recomputation and aborts on a mismatch).
* `--native`: adds `-march=native` (g++/clang++ only; not portable). `--clean`: removes build outputs.

## Run

```
solver <task_file> <solution_file> [--depth N] [--weight W] [--stall K] [--beam B] [--time T]
```

| Option | Meaning | Default |
|---|---|---|
| `--depth N` | max slides per precomputed move (memory grows ~3x per level; capped at 31) | 10 |
| `--weight W` | weight of h against cost, ranking by `g + W*h` | 1.0 |
| `--stall K` | iterations without progress that trigger the fallback | 50 |
| `--beam B` | beam width; `1` = greedy with tabu; omitted = auto (<=400 cells: 4, <=2500: 2, else 1) | auto |
| `--time T` | total time budget in seconds (precompute <= 30%, beam search until 70%, rest for fallback/output) | 10 |

On success the solution file is written, the objective `F = 100*(N*M-1) - total cost` is printed to stdout, exit code 0.
Unsolvable board, `ORDER` in the input, malformed input, unwritable solution file, bad options: non-zero exit,
no output file is left behind (an already existing file is not touched). The writability of the solution file is
checked before anything else. Set `SP_VERBOSE=1` for a one-line diagnostic summary on stderr.

Options may also be written `--time=40`; surrounding quotes/spaces and a decimal comma in values are tolerated.
The usage line requires both file names: `solver task.txt solution.txt --depth 12 --time 40`.

## Layout

| File | Content |
|---|---|
| `src/common.hpp` | types; `EMPTY_STEP` is the single place mapping tile direction to empty-cell direction |
| `src/problem.*` | parsing, O(NM) cycle-count solvability test, solution replay + output text |
| `src/move_table.*` | stage 1: moves and sparse swaps, cheapest move per swap, margins, grouped by length |
| `src/heuristic.hpp` | `Scorer`: heuristic, Zobrist hash, goal count, incremental `evaluate()` (swap-able interface) |
| `src/beam.*` | stage 2: beam search over swaps, tabu set (ring buffer + hash set), far/near phase, stall detection |
| `src/fallback.*` | constructive solver: rows, then columns, then the last 2x2; cheapest-path empty routing; works only inside the bottom-right rectangle that holds all misplaced tiles |
| `src/peephole.*` | post-pass: replaces stretches of the final solution by the cheaper equivalent stored in the move table |
| `src/main.cpp` | command line, pipeline, time budget, verification, output |

## Assumptions and small deviations (where the prompt was silent or ambiguous)

1. **P = N*M-1**, so `F = 100*(N*M-1) - cost`; an already solved board gives an empty `ORDER` and `F = 100*P`.
2. **Input parsing** is token based: `/` and `=` are separate tokens, so layouts such as `3 =/` or a `/` on its own line
   are accepted; `--` starts a comment that runs to the end of the line; keywords are case-insensitive; `COST` may precede `DATA`; CRLF is fine. Costs must be integers in
   `[0, 10^9]`, boards at most 65535 cells. The empty cell must be written `=`.
3. **Output** contains the re-serialised `DATA` section and the `ORDER` section, no `COST` section.
4. **Heuristic**: direction-weighted Manhattan distance; linear conflicts are not included.
5. **Far phase** threshold = `maxLen * (largest direction cost)`, decided per beam entry from its own h.
6. **Stall** counts an iteration as progress if the best `g + W*h` *or* the best h reaches a new minimum. With W = 1 and
   a consistent h the score `g + h` can never decrease, so the score alone would flag every run as stalled after K
   iterations.
7. **Fallback start**: the best entry ever seen by h (not only the current beam) is handed to the fallback; its path is
   kept.
8. **Safety net (addition)**: a beam prefix can cost more than solving constructively from the start (the far phase
   forces 10-slide moves). When the time budget allows, the fallback is also run from the initial board and the cheaper
   complete solution is written. It never makes a result worse; it can be removed in `main.cpp` ("Candidate B").
9. **Candidate ranking** is done lazily on a growing sorted prefix (tabu/duplicate filtering applied while scanning),
   which gives the same survivors as filtering all candidates first. Ties are broken by smaller h, then parent/move id.
10. **Precompute** stops at the end of the slide where 30% of the budget is used (a partly built level is kept, its moves
    are all valid) and is capped at 6M moves as a memory guard. A stored move is replaced by a cheaper longer one with
    the same swap and re-expanded (this does happen with unequal costs).
11. The margins are stored per move. As the prompt states, moves with equal swaps have equal margins (checked at depth 10: 1,104 duplicate swaps, none with different margins).
12. The constructive fallback solves the last two cells of a line with the usual corner trick and resolves the rare
    trapped corner with an exhaustive search over a window of at most 8 cells.
13. **Peephole pass (addition)**: the finished solution is scanned once; any window of up to `min(depth, 12)` slides whose
    swap equals the swap of a cheaper (or equally cheap, shorter) stored move that is legal at that spot is replaced
    (identity windows such as `U` then `D` disappear). The board effect is identical by construction. It runs until 90%
    of `--time`. Typical gain: 3-8% on random boards, about 1% on nearly solved boards. The move table is therefore kept
    in memory until the end of the run.
14. **Fallback region (addition)**: the constructive solver locks everything outside the bottom-right rectangle that
    contains the empty cell and all misplaced tiles, so nearly solved boards are not disturbed far from the problem.
    For a fully scrambled board the rectangle is the whole board (no change in behaviour).
