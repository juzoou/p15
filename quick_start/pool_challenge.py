import csv
import os
import subprocess

from build_subprojects import locate_solvers
from utils.measure_process import run
from utils.ensure_cwd import assert_cwd
assert_cwd()

TIMEOUT = 35
CHECKER = os.path.join("scripts", "check_solve_ipc.py")
CHECKER_CMD = f"python3 {CHECKER}"
POOL_DIR = "test_pool"
INDEX_PATH = os.path.join(POOL_DIR, "index.csv")
SOLUTIONS_DIR = "test_solutions"


# ----------------------------------------------------------------------------
# Task discovery and weights
# ----------------------------------------------------------------------------

def load_index(path=INDEX_PATH):
    """file name -> row dict from index.csv (empty if missing)."""
    if not os.path.isfile(path):
        return {}
    with open(path, newline="", encoding="utf-8") as f:
        return {row["file"]: row for row in csv.DictReader(f)}


def find_tasks(pool_dir=POOL_DIR):
    """All *.txt files in the pool directory (via os.listdir)."""
    return sorted(
        name for name in os.listdir(pool_dir)
        if name.lower().endswith(".txt") and os.path.isfile(os.path.join(pool_dir, name))
    )


def task_weight(info):
    """Weight of a task. Bigger boards weigh more: weight ~ rows * cols.
    Tasks missing from index.csv get weight 1."""
    if not info:
        return 1.0
    try:
        return float(int(info["rows"]) * int(info["cols"]))
    except (KeyError, ValueError):
        return 1.0


# ----------------------------------------------------------------------------
# Running / verifying one solver on one task
# ----------------------------------------------------------------------------

def run_test(cmd, in_path, out_path, timeout=TIMEOUT):
    with open(out_path, "w"):
        pass

    full_cmd = f"{cmd} {in_path} {out_path}"
    code, output, stat = run(full_cmd, f"Running '{full_cmd}'...", timeout=timeout)
    try:
        score = int(output.strip().split("\n")[-1])
        error = ""
    except Exception:
        score = 0
        error = output.replace("\n", " ")

    ok = code == 0
    time = stat["time"]
    return ok, score, time, error


def verify_test(out_path, in_path, checker=CHECKER_CMD):
    # NOTE: the checker also gets the task path, since tasks now differ.
    # If your checker takes only the output path, drop in_path here.
    cmd = f"{checker} {out_path} {in_path}"
    sp = subprocess.run(cmd, capture_output=True, text=True, shell=True)
    res = sp.stdout.strip().split("\n")
    if len(res) == 2:
        solved = res[0] == "OK"
        try:
            score = int(res[1])
        except ValueError:
            solved, score = False, 0
    else:
        solved = False
        score = 0
    return solved, score


# ----------------------------------------------------------------------------
# Ranking
# ----------------------------------------------------------------------------

def rank_points(task_results):
    """task_results: {label: (run_res, verify_res)} for ONE task.

    Solvers that solved the task are ranked by verified score (higher is
    better, as in the original test). Ties share a place (1, 1, 3, ...).
    A solver at place p among n solvers that solved the task gets n - p + 1
    points, so the best one gets the most; solvers that didn't solve get 0.
    Returns {label: (place or None, points)}.
    """
    solved = [(label, v[1]) for label, (r, v) in task_results.items() if v[0]]
    solved.sort(key=lambda x: -x[1])
    n = len(solved)

    out = {label: (None, 0) for label in task_results}
    place = 0
    prev = None
    for i, (label, score) in enumerate(solved, start=1):
        if score != prev:
            place = i
            prev = score
        out[label] = (place, n - place + 1)
    return out


# ----------------------------------------------------------------------------
# Verdict tokens (adapted from the original)
# ----------------------------------------------------------------------------

def verdict_of(run_res, verify_res):
    sp_ok, score, time, error = run_res
    solved, true_score = verify_res

    match (sp_ok, solved, score == true_score):
        case (True, True, True):   return "ok"
        case (True, True, False):  return "ok   (liar)"
        case (True, False, _):     return "fail (part)"
        case _:                    return "fail (exit)"


def print_table(table, sep="  "):
    if not table or not table[0]:
        return
    num_cols = len(table[0])
    col_widths = [0] * num_cols
    for row in table:
        for i in range(num_cols):
            col_widths[i] = max(col_widths[i], len(row[i]))
    for row in table:
        print(sep.join(row[i].ljust(col_widths[i]) for i in range(num_cols)))


# ----------------------------------------------------------------------------
# Main
# ----------------------------------------------------------------------------

if __name__ == "__main__":
    authors = "ajt"

    solvers = list(locate_solvers(authors))
    index = load_index()
    tasks = find_tasks()
    if not tasks:
        raise SystemExit(f"No .txt tasks found in {POOL_DIR}")
    if index:
        missing = [t for t in tasks if t not in index]
        if missing:
            print(f"Warning: {len(missing)} task(s) not in index.csv, weight=1: {missing[:5]}...")

    weights = {t: task_weight(index.get(t)) for t in tasks}
    total_weight = sum(weights.values())

    # Per-solver aggregates
    agg = {label: dict(points=0.0, solved=0, solved_w=0.0, time=0.0, liar=0, fails=0)
           for _, label in solvers}

    for ti, task in enumerate(tasks, start=1):
        in_path = os.path.join(POOL_DIR, task)
        print(f"[{ti}/{len(tasks)}] {task} (weight {weights[task]:g})")

        task_results = {}
        for cmd, label in solvers:
            out_dir = os.path.join(SOLUTIONS_DIR, label)
            os.makedirs(out_dir, exist_ok=True)
            out_path = os.path.join(out_dir, task)

            run_res = run_test(cmd, in_path, out_path)
            verify_res = verify_test(out_path, in_path)
            task_results[label] = (run_res, verify_res)

        ranking = rank_points(task_results)
        for label, (run_res, verify_res) in task_results.items():
            place, points = ranking[label]
            a = agg[label]
            a["points"] += points * weights[task]
            a["time"] += run_res[2]
            if verify_res[0]:
                a["solved"] += 1
                a["solved_w"] += weights[task]
                if verify_res[1] != run_res[1]:
                    a["liar"] += 1
            else:
                a["fails"] += 1

    # Final table, sorted by weighted points (best first)
    order = sorted(agg, key=lambda l: (-agg[l]["points"], agg[l]["time"]))
    n_tasks = len(tasks)
    header = ("Algorithm", "Points", "Solved", "Solved(w)", "Liars", "Time total", "Time avg")
    data = [header]
    for label in order:
        a = agg[label]
        data.append((
            label,
            f"{a['points'] / total_weight:.3f}",
            f"{a['solved']}/{n_tasks}",
            f"{100 * a['solved_w'] / total_weight:.1f}%",
            str(a["liar"]),
            f"{a['time']:.1f}",
            f"{a['time'] / n_tasks:.2f}",
        ))
    print()
    print_table(data)
