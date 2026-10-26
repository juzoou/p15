#!/usr/bin/env python3
"""Smoke/regression tests for Weighted A* (build with CMake first)."""
import pathlib
import subprocess
import sys
from support import binary_path
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
BIN = binary_path()
SAMPLE = ROOT / "examples" / "sample_2x3.txt"


def run(args, expect=0):
    p = subprocess.run([str(BIN), *map(str, args)], text=True, capture_output=True)
    if p.returncode != expect:
        raise AssertionError(f"command failed ({p.returncode}): {p.stdout}\n{p.stderr}")
    return p.stdout + p.stderr


def main():
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        astar_out, wa1_out, wa2_out, checked = (tmp / n for n in ("astar.txt", "wa1.txt", "wa2.txt", "checked.txt"))
        astar = run([SAMPLE, astar_out, "--algorithm", "astar"])
        wa1 = run([SAMPLE, wa1_out, "--algorithm", "wastar", "--weight", "1"])
        wa2 = run([SAMPLE, wa2_out, "--algorithm", "wastar", "--weight", "2.5"])
        assert "cost=1" in astar and "cost=1" in wa1, (astar, wa1)
        assert "weight=2.500000" in wa2 and "cost=1" in wa2, wa2
        check = run([wa2_out, checked])
        assert "CHECK: VALID" in check, check
        invalid = run([SAMPLE, tmp / "invalid.txt", "--algorithm", "wastar", "--weight", "0.5"], expect=2)
        assert "weight" in invalid.lower(), invalid
    print("Weighted A*: 4 checks passed")


if __name__ == "__main__":
    main()
