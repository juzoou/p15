import subprocess
import os

from build_subprojects import locate_solvers
from utils.run_process import run as simple_run
from utils.measure_process import run
from utils.ensure_cwd import assert_cwd
assert_cwd()

TIMEOUT = 35
CHECKER = r"python3 scripts\check_solve_ipc.py"


def run_test(in_path, out_path, timeout=TIMEOUT):
    with open(out_path, "w"): pass
    
    full_cmd = f"{cmd} {in_path} {out_path}"
    code, output, stat = run(full_cmd, f"Running '{full_cmd}'...", timeout=timeout)
    try:
        score = int(output.strip().split("\n")[-1])
        error = ""
    except Exception as e:
        score = 0
        error = output.replace("\n", " ")

    ok = code == 0
    time = stat['time']
    return ok, score, time, error


def verify_test(out_path, checker=CHECKER):
    cmd = f"{checker} {out_path}"
    sp = subprocess.run(cmd, capture_output=True, text=True)
    res = sp.stdout.strip().split("\n")
    if len(res) == 2:
        solved = res[0] == "OK"
        score = int(res[1])
    else:
        solved = False
        score = 0
    return solved, score


def get_verdict_tokens(label, run_res, verify_res):
    sp_ok, score, time, error = run_res
    solved, true_score = verify_res

    match (sp_ok, solved, score == true_score):
        case (True, True, True):   solution_verdict = "ok"
        case (True, True, False):  solution_verdict = "ok   (liar)"
        case (True, False, True):  solution_verdict = "fail (part)"
        case (True, False, False): solution_verdict = "fail (part)"
        case _:                    solution_verdict = "fail (exit)"

    score_token = str(true_score) if score == true_score else f"{true_score} ({score})"
    time_token = f"{time:.1f}"
    return label, solution_verdict, score_token, time_token, error


def get_verdict_header():
    return "Algoruthm", "Solution", "Score", "Time", "Error"


def print_table(table, sep="  "):
    if not table or not table[0]:
        return
    num_cols = len(table[0])
    col_widths = [0] * num_cols
    for row in table:
        for i in range(num_cols):
            col_widths[i] = max(col_widths[i], len(row[i]))
    for row in table:
        formatted_row = sep.join(row[i].ljust(col_widths[i]) for i in range(num_cols))
        print(formatted_row)


if __name__ == "__main__":
    authors = "ajts"

    res = []
    for cmd, label in locate_solvers(authors):
        in_path = r"tests\task89x98.txt"
        out_path = rf"test_solutions\{label}.txt"
        run_res = run_test(in_path, out_path)
        verify_res = verify_test(out_path)
        res.append((label, run_res, verify_res))
    
    res.sort(key=lambda r: -r[2][1])
    data = [get_verdict_header()] + [get_verdict_tokens(l, r, v) for l, r, v in res]
    print_table(data)

