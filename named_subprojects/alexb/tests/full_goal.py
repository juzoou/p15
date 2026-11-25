"""Regression tests for goal mode. Run after build.sh or build.bat."""
from pathlib import Path
import os
import random
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from check import parse

ALGOS = ("beam", "reduce", "exact", "greedy", "ida", "best")


def execute(algorithm, task, solution, *options):
    exe = ROOT / "bin" / ("p15_" + algorithm + (".exe" if os.name == "nt" else ""))
    return subprocess.run([exe, task, solution, *options], text=True, capture_output=True, timeout=10)


def check_solution(task, solution, output):
    data, result = parse(task), parse(solution)
    assert all(data[key] == result[key] for key in ("R", "C", "cells", "cost"))
    board = list(data["cells"])
    for tile, direction in result["order"]:
        i, blank = board.index(tile), board.index(0)
        r, c = divmod(i, data["C"])
        dr, dc = dict(U=(-1, 0), D=(1, 0), L=(0, -1), R=(0, 1))[direction]
        assert (r + dr, c + dc) == divmod(blank, data["C"])
        board[blank], board[i] = tile, 0
    assert board == list(range(1, len(board))) + [0], board
    cost = sum(data["cost"]["UDLR".index(d)] for _, d in result["order"])
    assert int(output) == 100 * (len(board) - 1) - cost


def scramble(rows, cols, steps, seed):
    rng = random.Random(seed)
    blank = rows * cols - 1
    a = list(range(1, blank + 1)) + [0]
    previous = -1
    for _ in range(steps):
        r, c = divmod(blank, cols)
        neighbors = [y * cols + x for y, x in ((r+1,c), (r-1,c), (r,c+1), (r,c-1)) if 0 <= y < rows and 0 <= x < cols]
        target = rng.choice([n for n in neighbors if n != previous] or neighbors)
        a[blank], a[target] = a[target], 0
        previous, blank = blank, target
    return a


def write_task(path, rows, cols, cells, costs):
    board = [" ".join(str(t) if t else "=" for t in cells[i:i+cols]) for i in range(0, len(cells), cols)]
    path.write_text("\n".join(["DATA", f"SIZE {rows} {cols}", *board, "/", "COST", costs, "/", ""]), encoding="utf-8")


def main():
    with tempfile.TemporaryDirectory() as folder:
        task, answer = Path(folder) / "in.txt", Path(folder) / "out.txt"
        cases = [(2, 2, [1, 2, 0, 3], "0 0 500 0"), (1, 5, [1, 0, 2, 3, 4], "0 0 800 0"),
                 (4, 1, [0, 1, 2, 3], "900 0 0 0"), (2, 3, [1, 2, 3, 4, 5, 0], "1 1 1 1"),
                 (3, 4, scramble(3, 4, 26, 3), "0 0 0 0"), (4, 5, scramble(4, 5, 60, 11), "4 2 8 1")]
        for rows, cols, cells, costs in cases:
            write_task(task, rows, cols, cells, costs)
            for algo in ALGOS:
                result = execute(algo, task, answer, "--time", "2")
                assert result.returncode == 0, (algo, rows, cols, result.stderr)
                check_solution(task, answer, result.stdout)
        write_task(task, 2, 2, [1, 2, 0, 3], "0 0 500 0")
        assert execute("exact", task, answer, "--partial").stdout.strip() == "200"
        write_task(task, 2, 2, [2, 1, 3, 0], "1 1 1 1")
        for algo in ALGOS:
            result = execute(algo, task, answer)
            assert result.returncode == 5 and "NO SOLUTION" in result.stderr
        verify = ROOT / "tests" / "samples" / "example_3x4_order.txt"
        assert execute("beam", verify, answer).stdout.strip() == "1098"
    print("OK")


if __name__ == "__main__":
    main()
