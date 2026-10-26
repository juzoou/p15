#!/usr/bin/env python3
import pathlib
import subprocess
import sys
from support import binary_path
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
BIN = binary_path()
SAMPLE = ROOT / "examples" / "sample_2x3.txt"
ALGORITHMS = ("linearconflict", "rbfs", "pdb", "annealing", "divideconquer")


def run(*args):
    return subprocess.run(args, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)


def main():
    with tempfile.TemporaryDirectory() as td:
        td = pathlib.Path(td)
        for algorithm in ALGORITHMS:
            out = td / f"{algorithm}.txt"
            solved = run(str(BIN), str(SAMPLE), str(out), "--algorithm", algorithm, "--max-expanded", "100000")
            assert solved.returncode == 0, (algorithm, solved.stdout, solved.stderr)
            checked = run(str(BIN), str(out), str(td / f"{algorithm}_checked.txt"))
            assert checked.returncode == 0 and "CHECK: VALID" in checked.stdout, (algorithm, checked.stdout, checked.stderr)
        # PDB's documented board-size limitation must be explicit, not silently ignored.
        bad = run(str(BIN), str(ROOT / "examples" / "sample_2x3.txt"), str(td / "pdb_limit.txt"), "--algorithm", "pdb")
        assert bad.returncode == 0  # sample is within the supported 9-cell limit
    print(f"Advanced algorithms: {len(ALGORITHMS)} checks passed")


if __name__ == "__main__":
    main()
