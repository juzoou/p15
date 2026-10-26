"""Run the original teacher files and compare with explicit expected outcomes."""
import argparse
import csv
import json
from pathlib import Path
import re
import subprocess
import time

try:
    from .build_cpp import ROOT, build_solver
    from .parse_vasekin import _parse_input, read_input_text, InputError
    from .check_solve import _check_order, is_solvable
except ImportError:
    from build_cpp import ROOT, build_solver
    from parse_vasekin import _parse_input, read_input_text, InputError
    from check_solve import _check_order, is_solvable


def reference(path):
    try:
        puzzle = _parse_input(read_input_text(path))
        if puzzle.order is not None:
            checked = _check_order(puzzle)
            return "valid", checked
        return ("valid" if is_solvable(puzzle) else "unreachable"), None
    except (InputError, UnicodeError):
        return "invalid", None


def run_suite(binary=None, output_dir=None, report_path=None):
    binary = binary or build_solver()
    output_dir = Path(output_dir or ROOT / "artifacts" / "teacher")
    report_path = Path(report_path or ROOT / "reports" / "teacher_results.csv")
    output_dir.mkdir(parents=True, exist_ok=True)
    expectations = json.loads((ROOT / "tests" / "teacher_expected.json").read_text(encoding="utf-8"))
    results = []
    for name, expected in expectations.items():
        path = ROOT / "tests" / "teacher" / name
        output = output_dir / name
        started = time.perf_counter()
        process = subprocess.run([str(binary), str(path), str(output)], capture_output=True,
                                 encoding="utf-8", errors="replace", timeout=15)
        elapsed = time.perf_counter() - started
        (output_dir / f"{path.stem}.log").write_text(process.stdout + process.stderr, encoding="utf-8")
        observed = "valid" if process.returncode == 0 else (
            "unreachable" if "puzzle is unreachable" in process.stderr else "invalid")
        reference_status, reference_check = reference(path)
        passed = observed == expected["status"] == reference_status
        if observed != "valid":
            passed &= process.returncode == 2 and expected.get("error_contains", "unreachable") in process.stderr
        score = solved = ""
        detail = process.stderr.strip()
        if observed == "valid":
            try:
                source = _parse_input(read_input_text(path))
                emitted = _parse_input(read_input_text(output))
                checked = _check_order(emitted)
                score, solved = checked.score, checked.solved
                reported = re.search(r"(?:^|; )F=(-?\d+)", process.stdout)
                passed &= bool(reported) and int(reported.group(1)) == score
                passed &= (source.board, source.rows, source.cols, source.costs) == (
                    emitted.board, emitted.rows, emitted.cols, emitted.costs)
                if source.order is not None:
                    passed &= source.order == emitted.order
                    passed &= reference_check.score == score
                passed &= score == expected["score"] and solved == expected["solved"]
            except (ValueError, OSError) as error:
                passed = False
                detail = str(error)
        results.append(dict(file=name, expected=expected["status"], observed=observed,
                            python_status=reference_status, exit_code=process.returncode,
                            solved=solved, score=score, passed=bool(passed),
                            seconds=f"{elapsed:.6f}", detail=detail))
        print(f"{'PASS' if passed else 'FAIL'} {name}: {observed}" +
              (f", solved={solved}, F={score}" if observed == "valid" else ""))
    report_path.parent.mkdir(parents=True, exist_ok=True)
    with report_path.open("w", encoding="utf-8-sig", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(results[0]))
        writer.writeheader()
        writer.writerows(results)
    print(f"Teacher tests: {sum(r['passed'] for r in results)}/{len(results)} passed")
    return results


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    raise SystemExit(0 if all(r["passed"] for r in run_suite(args.binary, args.output_dir, args.report)) else 1)
