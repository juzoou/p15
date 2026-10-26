#!/usr/bin/env python3
"""Integration tests for the transposed column-by-column constructive solver."""
from __future__ import annotations
import random
import subprocess
import sys
from support import binary_path
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCES = sorted((ROOT / "src").glob("*.cpp"))


def make_scramble(rows: int, cols: int, steps: int, rng: random.Random) -> list[int]:
    board = list(range(1, rows * cols)) + [0]
    blank, previous_blank = len(board) - 1, -1
    for _ in range(steps):
        r, c = divmod(blank, cols)
        choices = []
        if r > 0: choices.append(blank - cols)
        if r + 1 < rows: choices.append(blank + cols)
        if c > 0: choices.append(blank - 1)
        if c + 1 < cols: choices.append(blank + 1)
        if previous_blank in choices and len(choices) > 1:
            choices.remove(previous_blank)
        nxt = rng.choice(choices)
        board[blank], board[nxt] = board[nxt], board[blank]
        previous_blank, blank = blank, nxt
    return board


def write_input(path: Path, rows: int, cols: int, board: list[int]) -> None:
    lines = ["DATA", f"SIZE {rows} {cols}"]
    for r in range(rows):
        vals = board[r * cols:(r + 1) * cols]
        lines.append(" ".join("=" if x == 0 else str(x) for x in vals))
    lines.extend(["/", "COST", "2 3 1 4", "/"])
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def goal_text(rows: int, cols: int) -> str:
    vals = list(range(1, rows * cols)) + [0]
    return "\n".join(" ".join("=" if x == 0 else str(x)
                               for x in vals[r * cols:(r + 1) * cols])
                     for r in range(rows))


def run_case(binary: Path, temp: Path, rows: int, cols: int, steps: int, seed: int) -> None:
    board = make_scramble(rows, cols, steps, random.Random(seed))
    inp = temp / f"in_{rows}x{cols}_{seed}.txt"
    out = temp / f"out_{rows}x{cols}_{seed}.txt"
    checked = temp / f"checked_{rows}x{cols}_{seed}.txt"
    write_input(inp, rows, cols, board)
    solved = subprocess.run([str(binary), str(inp), str(out), "--algorithm", "columnbycolumn"],
                            text=True, capture_output=True, timeout=60)
    if solved.returncode != 0 or "RESULT: SOLVED" not in solved.stdout:
        raise AssertionError(f"columnbycolumn failed on {rows}x{cols}: {solved.stderr}\n{solved.stdout[:1000]}")
    verified = subprocess.run([str(binary), str(out), str(checked)], text=True, capture_output=True, timeout=60)
    if verified.returncode != 0 or "CHECK: VALID" not in verified.stdout:
        raise AssertionError(f"ORDER invalid on {rows}x{cols}: {verified.stderr}\n{verified.stdout[:1000]}")
    if f"placed={rows * cols - 1}" not in verified.stdout:
        raise AssertionError(f"not all tiles are placed on {rows}x{cols}")
    marker = "Final board:\n"
    if marker not in verified.stdout or verified.stdout.split(marker, 1)[1].strip() != goal_text(rows, cols):
        raise AssertionError(f"wrong final board on {rows}x{cols}")


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="pyatnashki_column_test_") as tmpdir:
        temp = Path(tmpdir)
        binary = binary_path()
        cases = [
            (2, 3, 80, 1101), (3, 2, 80, 1102), (3, 4, 80, 1103), (4, 3, 80, 1104),
            (4, 7, 150, 1105), (7, 4, 150, 1106), (3, 12, 200, 1107), (12, 3, 200, 1108),
            (6, 9, 300, 1109), (9, 6, 300, 1110), (25, 40, 1500, 1111), (40, 25, 1500, 1112),
            (100, 99, 1500, 1113), (99, 100, 1500, 1114), (100, 2, 1000, 1115), (2, 100, 1000, 1116),
        ]
        for index, (rows, cols, steps, seed) in enumerate(cases, 1):
            run_case(binary, temp, rows, cols, steps, seed)
            print(f"[{index:02d}/{len(cases)}] OK columnbycolumn {rows}x{cols}")
        print(f"PASS: {len(cases)} column-by-column cases; every ORDER validated.")


if __name__ == "__main__":
    main()
