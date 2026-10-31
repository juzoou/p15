#!/usr/bin/env python3
"""Benchmark runner for the p15 solvers.

Usage:
    python benchmark.py <tasks_dir> <solutions_dir>

The script discovers available solvers, runs every solver on every *.txt task,
prints progress, saves solver outputs, and writes two CSV reports:
    benchmark_results.csv  - one row per task/solver run;
    benchmark_summary.csv  - aggregate results by algorithm and board size.

Environment variables (optional):
    P15_TIMEOUT_SECONDS - timeout for one solver run, default: 60 seconds;
    P15_SOLVER_DIR      - extra directory where solver binaries are searched.
"""

from __future__ import annotations

import csv
import os
import re
import subprocess
import sys
import time
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from statistics import mean
from typing import Iterable


DEFAULT_TIMEOUT_SECONDS = 60.0
TASK_EXTENSION = ".txt"
RESULTS_FILE_NAME = "benchmark_results.csv"
SUMMARY_FILE_NAME = "benchmark_summary.csv"


@dataclass(frozen=True)
class SolverSpec:
    key: str
    display_name: str


@dataclass(frozen=True)
class Solver:
    spec: SolverSpec
    path: Path


SOLVER_SPECS = (
    SolverSpec("solver_idastar", "IDA*"),
    SolverSpec("solver_weighted_astar", "Weighted A*"),
    SolverSpec("solver_constructive", "Constructive"),
    SolverSpec("solver_constructive_weighted", "Constructive weighted"),
)


NUMERIC_LINE_RE = re.compile(r"^[+-]?(?:\d+(?:\.\d*)?|\.\d+)$")
NATURAL_PART_RE = re.compile(r"(\d+)")


def natural_key(path: Path) -> list[object]:
    """Return a key that sorts t2.txt before t10.txt."""
    return [int(part) if part.isdigit() else part.lower() for part in NATURAL_PART_RE.split(path.name)]


def timeout_seconds() -> float:
    """Read the per-run timeout from the environment."""
    raw = os.environ.get("P15_TIMEOUT_SECONDS", str(DEFAULT_TIMEOUT_SECONDS))
    try:
        value = float(raw)
    except ValueError as exc:
        raise ValueError(f"P15_TIMEOUT_SECONDS must be a number, got: {raw!r}") from exc
    if value <= 0:
        raise ValueError("P15_TIMEOUT_SECONDS must be greater than zero")
    return value


def candidate_solver_dirs(script_dir: Path) -> list[Path]:
    """Return cross-platform locations where CMake normally places binaries."""
    dirs: list[Path] = []
    extra = os.environ.get("P15_SOLVER_DIR")
    if extra:
        dirs.append(Path(extra).expanduser().resolve())
    dirs.extend(
        [
            script_dir / "build" / "Release",
            script_dir / "build" / "Debug",
            script_dir / "build",
            script_dir,
        ]
    )

    unique: list[Path] = []
    seen: set[Path] = set()
    for directory in dirs:
        resolved = directory.resolve()
        if resolved not in seen:
            seen.add(resolved)
            unique.append(resolved)
    return unique


def find_solver(spec: SolverSpec, search_dirs: Iterable[Path]) -> Solver | None:
    """Find a solver executable or Python script."""
    names = [f"{spec.key}.exe", spec.key, f"{spec.key}.py"] if os.name == "nt" else [spec.key, f"{spec.key}.py"]
    for directory in search_dirs:
        for name in names:
            path = directory / name
            if path.is_file():
                if path.suffix.lower() == ".py" or os.name == "nt" or os.access(path, os.X_OK):
                    return Solver(spec, path)
    return None


def discover_solvers(script_dir: Path) -> list[Solver]:
    """Discover all known solvers that are currently built/available."""
    search_dirs = candidate_solver_dirs(script_dir)
    solvers: list[Solver] = []
    for spec in SOLVER_SPECS:
        solver = find_solver(spec, search_dirs)
        if solver is not None:
            solvers.append(solver)
        else:
            print(f"WARNING: {spec.display_name}: solver not found, skipping", file=sys.stderr)
    return solvers


def strip_comments(text: str) -> str:
    """Remove '-- ...' comments used by the original task format."""
    return "\n".join(line.split("--", 1)[0] for line in text.splitlines())


def detect_board_size(task_path: Path) -> str:
    """Return board size as '<rows>x<cols>' for either supported input format."""
    try:
        text = task_path.read_text(encoding="utf-8-sig", errors="replace")
    except OSError:
        return "?"

    cleaned = strip_comments(text)
    match = re.search(r"\bSIZE\s+([+-]?\d+)\s+([+-]?\d+)", cleaned, flags=re.IGNORECASE)
    if match:
        rows, cols = int(match.group(1)), int(match.group(2))
        return f"{rows}x{cols}" if rows > 0 and cols > 0 else "?"

    tokens = cleaned.split()
    if len(tokens) >= 6:
        try:
            width = int(tokens[4])
            height = int(tokens[5])
        except ValueError:
            return "?"
        if width > 0 and height > 0:
            return f"{height}x{width}"
    return "?"


def parse_score(stdout: str) -> str | None:
    """Extract the score from stdout; the solver contract says it prints one number."""
    for line in reversed(stdout.splitlines()):
        candidate = line.strip().replace(",", ".")
        if NUMERIC_LINE_RE.fullmatch(candidate):
            return candidate
    return None


def compact_message(stdout: str, stderr: str, limit: int = 500) -> str:
    """Return a compact diagnostic message suitable for one CSV cell."""
    stderr = stderr.strip()
    stdout = stdout.strip()
    message = stderr if stderr else stdout
    message = " ".join(message.split())
    if len(message) > limit:
        return message[: limit - 3] + "..."
    return message


def solver_command(solver: Solver, task_path: Path, solution_path: Path) -> list[str]:
    """Build a platform-independent subprocess command."""
    if solver.path.suffix.lower() == ".py":
        return [sys.executable, str(solver.path), str(task_path), str(solution_path)]
    return [str(solver.path), str(task_path), str(solution_path)]


def run_one(solver: Solver, task_path: Path, solution_path: Path, timeout: float) -> dict[str, object]:
    """Run one solver on one task and return a report row."""
    try:
        solution_path.unlink(missing_ok=True)
    except OSError as exc:
        return {
            "status": "OUTPUT_ERROR",
            "score": "",
            "time_ms": 0.0,
            "return_code": "",
            "message": f"Cannot remove old solution file: {exc}",
        }

    command = solver_command(solver, task_path, solution_path)
    started = time.perf_counter()
    try:
        completed = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=timeout, check=False)
    except subprocess.TimeoutExpired as exc:
        elapsed_ms = (time.perf_counter() - started) * 1000.0
        try:
            solution_path.unlink(missing_ok=True)
        except OSError:
            pass
        stdout = exc.stdout if isinstance(exc.stdout, str) else ""
        stderr = exc.stderr if isinstance(exc.stderr, str) else ""
        return {
            "status": "TIMEOUT",
            "score": "",
            "time_ms": elapsed_ms,
            "return_code": "",
            "message": compact_message(stdout, stderr) or f"Timeout after {timeout:g} s",
        }
    except OSError as exc:
        elapsed_ms = (time.perf_counter() - started) * 1000.0
        return {
            "status": "START_ERROR",
            "score": "",
            "time_ms": elapsed_ms,
            "return_code": "",
            "message": str(exc),
        }

    elapsed_ms = (time.perf_counter() - started) * 1000.0
    score = parse_score(completed.stdout)
    message = compact_message(completed.stdout, completed.stderr)

    if completed.returncode != 0:
        combined = f"{completed.stdout}\n{completed.stderr}".lower()
        status = "UNSOLVABLE" if "unsolvable" in combined or "no solution" in combined else "ERROR"
    elif not solution_path.is_file():
        status = "NO_SOLUTION_FILE"
    elif score is None:
        status = "BAD_SCORE"
    else:
        status = "OK"
        message = ""

    return {
        "status": status,
        "score": score or "",
        "time_ms": elapsed_ms,
        "return_code": completed.returncode,
        "message": message,
    }


def verify_output_dir(output_dir: Path) -> None:
    """Create the output directory and verify that files can be written there."""
    output_dir.mkdir(parents=True, exist_ok=True)
    probe = output_dir / ".p15_write_test.tmp"
    try:
        probe.write_text("ok", encoding="utf-8")
        probe.unlink()
    except OSError as exc:
        raise OSError(f"Cannot write to output directory '{output_dir}': {exc}") from exc


def write_results_csv(path: Path, rows: list[dict[str, object]]) -> None:
    """Write detailed benchmark results in an Excel-friendly UTF-8 CSV."""
    fieldnames = ["Файл", "Размер", "Алгоритм", "Статус", "Скор", "Время, мс", "Код возврата", "Файл решения", "Сообщение"]
    with path.open("w", encoding="utf-8-sig", newline="") as file:
        writer = csv.DictWriter(file, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def write_summary_csv(path: Path, rows: list[dict[str, object]]) -> None:
    """Write aggregate statistics by algorithm and board size."""
    grouped: dict[tuple[str, str], list[dict[str, object]]] = defaultdict(list)
    for row in rows:
        grouped[(str(row["Алгоритм"]), str(row["Размер"]))].append(row)

    fieldnames = ["Алгоритм", "Размер", "Тестов", "Решено", "Нерешаемых", "Таймаутов", "Ошибок", "Среднее время, мс", "Макс. время, мс"]
    summary_rows: list[dict[str, object]] = []

    def append_summary(algorithm: str, size: str, items: list[dict[str, object]]) -> None:
        times = [float(item["Время, мс"]) for item in items]
        statuses = [str(item["Статус"]) for item in items]
        summary_rows.append(
            {
                "Алгоритм": algorithm,
                "Размер": size,
                "Тестов": len(items),
                "Решено": statuses.count("OK"),
                "Нерешаемых": statuses.count("UNSOLVABLE"),
                "Таймаутов": statuses.count("TIMEOUT"),
                "Ошибок": sum(status not in {"OK", "UNSOLVABLE", "TIMEOUT"} for status in statuses),
                "Среднее время, мс": f"{mean(times):.3f}" if times else "",
                "Макс. время, мс": f"{max(times):.3f}" if times else "",
            }
        )

    for (algorithm, size), items in sorted(grouped.items(), key=lambda item: (item[0][0].lower(), item[0][1])):
        append_summary(algorithm, size, items)

    by_algorithm: dict[str, list[dict[str, object]]] = defaultdict(list)
    for row in rows:
        by_algorithm[str(row["Алгоритм"])].append(row)
    for algorithm, items in sorted(by_algorithm.items()):
        append_summary(algorithm, "ВСЕ", items)

    with path.open("w", encoding="utf-8-sig", newline="") as file:
        writer = csv.DictWriter(file, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(summary_rows)


def print_final_summary(rows: list[dict[str, object]]) -> None:
    """Print one concise summary line per algorithm."""
    by_algorithm: dict[str, list[dict[str, object]]] = defaultdict(list)
    for row in rows:
        by_algorithm[str(row["Алгоритм"])].append(row)

    print("\nSummary:")
    for algorithm, items in sorted(by_algorithm.items()):
        statuses = [str(item["Статус"]) for item in items]
        ok_times = [float(item["Время, мс"]) for item in items if item["Статус"] == "OK"]
        average = f"{mean(ok_times):.3f} ms" if ok_times else "n/a"
        errors = sum(status not in {"OK", "UNSOLVABLE", "TIMEOUT"} for status in statuses)
        print(f"  {algorithm}: OK={statuses.count('OK')}, TIMEOUT={statuses.count('TIMEOUT')}, UNSOLVABLE={statuses.count('UNSOLVABLE')}, ERRORS={errors}, avg OK time={average}")


def main() -> int:
    if len(sys.argv) != 3:
        print(f"Usage: {Path(sys.argv[0]).name} <tasks_dir> <solutions_dir>", file=sys.stderr)
        return 2

    tasks_dir = Path(sys.argv[1]).expanduser().resolve()
    output_dir = Path(sys.argv[2]).expanduser().resolve()
    script_dir = Path(__file__).resolve().parent

    if not tasks_dir.is_dir():
        print(f"ERROR: task directory does not exist: {tasks_dir}", file=sys.stderr)
        return 2

    try:
        verify_output_dir(output_dir)
        timeout = timeout_seconds()
    except (OSError, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2

    tasks = sorted((path for path in tasks_dir.iterdir() if path.is_file() and path.suffix.lower() == TASK_EXTENSION), key=natural_key)
    if not tasks:
        print(f"ERROR: no {TASK_EXTENSION} tasks found in: {tasks_dir}", file=sys.stderr)
        return 2

    solvers = discover_solvers(script_dir)
    if not solvers:
        print("ERROR: no solvers found. Build the project first or set P15_SOLVER_DIR.", file=sys.stderr)
        return 2

    print(f"Tasks: {len(tasks)}")
    print(f"Solvers: {', '.join(solver.spec.display_name for solver in solvers)}")
    print(f"Timeout: {timeout:g} s per run")
    print(f"Solutions: {output_dir}")
    print()

    total_runs = len(tasks) * len(solvers)
    run_index = 0
    rows: list[dict[str, object]] = []

    for task_path in tasks:
        board_size = detect_board_size(task_path)
        for solver in solvers:
            run_index += 1
            solution_name = f"{task_path.stem}_{solver.spec.key}.txt"
            solution_path = output_dir / solution_name
            result = run_one(solver, task_path, solution_path, timeout)

            row = {
                "Файл": task_path.name,
                "Размер": board_size,
                "Алгоритм": solver.spec.display_name,
                "Статус": result["status"],
                "Скор": result["score"],
                "Время, мс": f"{float(result['time_ms']):.3f}",
                "Код возврата": result["return_code"],
                "Файл решения": solution_name if result["status"] == "OK" else "",
                "Сообщение": result["message"],
            }
            rows.append(row)

            score_text = f", score={result['score']}" if result["score"] != "" else ""
            print(f"[{run_index:>4}/{total_runs}] {task_path.name:<20} | {solver.spec.display_name:<23} | {str(result['status']):<16} | {float(result['time_ms']):>9.3f} ms{score_text}")

    results_path = output_dir / RESULTS_FILE_NAME
    summary_path = output_dir / SUMMARY_FILE_NAME
    write_results_csv(results_path, rows)
    write_summary_csv(summary_path, rows)
    print_final_summary(rows)

    print(f"\nDetailed CSV: {results_path}")
    print(f"Summary CSV:  {summary_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
