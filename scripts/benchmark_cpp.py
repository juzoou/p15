"""Exercise every C++ mode on valid DATA from tests, verifying each emitted ORDER."""
import argparse
import csv
import json
from pathlib import Path
import re
import subprocess
import sys
import time
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from generate_task import render_task
from scripts.build_cpp import build_solver
from scripts.parse_vasekin import _parse_input, read_input_text
from scripts.check_solve import _check_order

ALGORITHMS = ("auto", "astar", "linearconflict", "wastar", "dijkstra", "bidijkstra",
              "idastar", "rbfs", "01bfs", "dial", "pdb", "annealing",
              "rowbyrow", "columnbycolumn", "divideconquer")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--timeout", type=float, default=10)
    parser.add_argument("--max-expanded", type=int, default=200000)
    parser.add_argument("--large-max-expanded", type=int, default=500)
    args = parser.parse_args()
    if args.timeout <= 0 or args.max_expanded <= 0 or args.large_max_expanded <= 0:
        parser.error("budgets must be positive")
    binary = build_solver()
    output_dir = ROOT / "artifacts" / "algorithm_benchmark"
    output_dir.mkdir(parents=True, exist_ok=True)
    expected = json.loads((ROOT / "tests" / "teacher_expected.json").read_text())
    tasks = []
    for name, case in expected.items():
        if case["status"] == "valid":
            tasks.append(("teacher_" + Path(name).stem,
                          _parse_input(read_input_text(ROOT / "tests" / "teacher" / name))))
    for archive, member in [("5x4_test.zip", "task5x4.txt"), ("89x98_test.zip", "task89x98.txt")]:
        with zipfile.ZipFile(ROOT / "tests" / archive) as zipped:
            tasks.append((Path(member).stem, _parse_input(zipped.read(member).decode("utf-8-sig"))))
    rows = []
    report = ROOT / "reports" / "algorithm_results.csv"
    report.parent.mkdir(parents=True, exist_ok=True)
    for name, puzzle in tasks:
        input_path = output_dir / f"{name}_input.txt"
        # Deliberately omit ORDER: these runs must solve, not check stored answers.
        input_path.write_text(render_task(puzzle.rows, puzzle.cols, list(puzzle.board),
                                         puzzle.costs, 0, 0), encoding="utf-8")
        limit = args.large_max_expanded if len(puzzle.board) > 100 else args.max_expanded
        for algorithm in ALGORITHMS:
            output = output_dir / f"{name}_{algorithm}.txt"
            unsupported = ((algorithm == "pdb" and (len(puzzle.board) > 9 or any(c != 1 for c in puzzle.costs)))
                           or (algorithm == "01bfs" and any(c > 1 for c in puzzle.costs))
                           or (algorithm == "dial" and max(puzzle.costs) > 100000))
            record = dict(task=name, size=f"{puzzle.rows}x{puzzle.cols}", algorithm=algorithm,
                          status="", moves="", cost="", score="", expanded="", solver_seconds="",
                          wall_seconds="", expansion_limit=limit, timeout_seconds=args.timeout, detail="")
            started = time.perf_counter()
            command = [str(binary), str(input_path), str(output), "--algorithm", algorithm,
                       "--max-expanded", str(limit)]
            try:
                result = subprocess.run(command, capture_output=True, encoding="utf-8", errors="replace",
                                        timeout=args.timeout)
                record["wall_seconds"] = f"{time.perf_counter() - started:.6f}"
                (output_dir / f"{name}_{algorithm}.log").write_text(result.stdout + result.stderr, encoding="utf-8")
                if result.returncode == 0:
                    emitted = _parse_input(read_input_text(output))
                    checked = _check_order(emitted)
                    assert (emitted.rows, emitted.cols, emitted.board, emitted.costs) == (
                        puzzle.rows, puzzle.cols, puzzle.board, puzzle.costs), "task changed"
                    assert checked.solved, "goal not reached"
                    for key, value in [("moves", checked.valid_moves), ("cost", checked.total_cost), ("F", checked.score)]:
                        match = re.search(r"(?:^|; )" + key + r"=(-?\d+)", result.stdout)
                        assert match and int(match.group(1)) == value, f"incorrect {key} in C++ summary"
                    assert not unsupported, "unsupported mode unexpectedly accepted"
                    record.update(status="SOLVED", moves=checked.valid_moves, cost=checked.total_cost, score=checked.score)
                    for key, target in [("expanded", "expanded"), ("seconds", "solver_seconds")]:
                        match = re.search(r"(?:^|; )" + key + r"=([^;\s]+)", result.stdout)
                        if match:
                            record[target] = match.group(1)
                else:
                    detail = result.stderr.strip()
                    record["detail"] = detail
                    if result.returncode != 2:
                        record["status"] = "ERROR"
                    elif unsupported and any(message in detail for message in (
                            "PDB mode currently", "0-1 BFS requires", "Dial requires")):
                        record["status"] = "UNSUPPORTED"
                    elif "search expansion limit exceeded" in detail:
                        record["status"] = "LIMIT"
                    elif "simulated annealing did not find" in detail:
                        record["status"] = "NO_SOLUTION_IN_BUDGET"
                    else:
                        record["status"] = "ERROR"
            except subprocess.TimeoutExpired:
                record.update(status="TIMEOUT", detail="process killed at wall-time limit",
                              wall_seconds=f"{time.perf_counter() - started:.6f}")
            except (AssertionError, ValueError, OSError) as error:
                record.update(status="ERROR", detail=str(error))
            rows.append(record)
            print(f"{name:14} {algorithm:15} {record['status']:23} moves={record['moves']} cost={record['cost']}", flush=True)
            with report.open("w", encoding="utf-8-sig", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=list(record))
                writer.writeheader()
                writer.writerows(rows)
    counts = {status: sum(row["status"] == status for row in rows) for status in sorted({r["status"] for r in rows})}
    print(json.dumps(counts), flush=True)
    return 1 if counts.get("ERROR") else 0


if __name__ == "__main__":
    raise SystemExit(main())
