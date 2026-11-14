import subprocess
import statistics
import sys

from build_subprojects import locate_solvers
from utils.measure_process import run
from utils.ensure_cwd import assert_cwd
assert_cwd()

TIMEOUT = 35
N_RUNS = 5
CHECKER = r"python3 scripts\check_solve_ipc.py"
BASE = "test_solutions"

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
    return ok, score, stat['time'], error


def verify_test(out_path, checker=CHECKER):
    cmd = f"{checker} {out_path}"
    sp = subprocess.run(cmd, capture_output=True, text=True)
    res = sp.stdout.strip().split("\n")
    if len(res) == 2:
        return res[0] == "OK", int(res[1])
    return False, 0


def run_solver_repeated(cmd, label, in_path, n_runs=N_RUNS, base=BASE):
    """Run one solver n_runs times; each run writes to its own output file."""
    runs = []
    for i in range(n_runs):
        out_path = rf"{base}\{label}_{i}.txt"
        run_res = run_test(cmd, in_path, out_path)
        verify_res = verify_test(out_path)
        runs.append((run_res, verify_res))
    return runs


def summarize(label, runs):
    n = len(runs)
    solved_count = sum(1 for (sp_ok, *_), (solved, _) in runs if sp_ok and solved)
    liar_count = sum(
        1 for (_, score, _, _), (solved, true_score) in runs
        if solved and score != true_score
    )
    scores = [true_score if solved else 0 for _, (solved, true_score) in runs]
    times = [t for (_, _, t, _), _ in runs]
    errors = {e for (_, _, _, e), _ in runs if e}

    mean_score = statistics.mean(scores)
    std_score = statistics.pstdev(scores)
    summary = (
        label,
        f"{solved_count}/{n}",
        f"{mean_score:.1f}",
        f"{std_score:.1f}",
        str(min(scores)),
        str(max(scores)),
        f"{statistics.mean(times):.1f}",
        f"{statistics.pstdev(times):.1f}",
        str(liar_count),
        next(iter(errors), ""),  # show one example error
    )
    return mean_score, summary


def get_header():
    return ("Algorithm", "Solved", "Mean", "Std", "Min", "Max",
            "T mean", "T std", "Liar", "Error")


def print_table(table, sep="  "):
    if not table or not table[0]:
        return
    num_cols = len(table[0])
    col_widths = [max(len(row[i]) for row in table) for i in range(num_cols)]
    for row in table:
        print(sep.join(row[i].ljust(col_widths[i]) for i in range(num_cols)))


if __name__ == "__main__":
    import os

    authors = "ajts"
    n_runs = int(sys.argv[1]) if len(sys.argv) > 1 else N_RUNS
    in_path = r"tests\task89x98.txt"
    os.makedirs(BASE, exist_ok=True)

    results = []
    for cmd, label in locate_solvers(authors):
        runs = run_solver_repeated(cmd, label, in_path, n_runs)
        results.append(summarize(label, runs))

    results.sort(key=lambda r: -r[0])  # best mean score first
    print_table([get_header()] + [s for _, s in results])