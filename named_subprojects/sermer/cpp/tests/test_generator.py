#!/usr/bin/env python3
"""Smoke/integration tests for the text task generator and modular C++ solvers."""
from __future__ import annotations
import subprocess
import sys
from support import binary_path
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCES = sorted((ROOT / "src").glob("*.cpp"))
GENERATOR = ROOT / "generate_task.py"


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="pyatnashki_generator_test_") as tmpdir:
        temp = Path(tmpdir)
        binary = binary_path()
        cases = [(1, 2), (1, 7), (7, 1), (1, 100), (100, 1), (2, 3), (3, 2),
                 (3, 4), (4, 3), (3, 3), (10, 15), (99, 100)]
        for idx, (rows, cols) in enumerate(cases, 1):
            inp = temp / f"task_{rows}x{cols}.txt"
            out = temp / f"sol_{rows}x{cols}.txt"
            checked = temp / f"check_{rows}x{cols}.txt"
            subprocess.run([sys.executable, str(GENERATOR), str(rows), str(cols), "--steps", "80",
                            "--seed", str(9000 + idx), "-o", str(inp)], check=True,
                           text=True, capture_output=True)
            algo = "columnbycolumn" if rows > cols else "rowbyrow"
            solved = subprocess.run([str(binary), str(inp), str(out), "--algorithm", algo],
                                    check=True, text=True, capture_output=True, timeout=60)
            assert "RESULT: SOLVED" in solved.stdout, solved.stdout
            verified = subprocess.run([str(binary), str(out), str(checked)], check=True,
                                      text=True, capture_output=True, timeout=60)
            assert "CHECK: VALID" in verified.stdout, verified.stdout
            assert f"placed={rows * cols - 1}" in verified.stdout, verified.stdout
            print(f"[{idx:02d}/{len(cases)}] OK generator {rows}x{cols} + {algo}")
        for rows, cols in [(0, 4), (101, 2), (1, 1)]:
            bad = subprocess.run([sys.executable, str(GENERATOR), str(rows), str(cols), "-o", str(temp / "bad.txt")],
                                 text=True, capture_output=True)
            assert bad.returncode != 0, f"invalid dimensions accepted: {rows}x{cols}"
        print(f"PASS: {len(cases)} generated tasks; invalid dimensions rejected.")


if __name__ == "__main__":
    main()
