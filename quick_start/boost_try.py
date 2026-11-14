from build_subprojects import TBoost
from utils.measure_process import run
from utils.ensure_cwd import assert_cwd
import subprocess
import os
assert_cwd()


BASE = "test_solutions"
OUT_BASE = "test_boost"
BOOST_TIME = 20
CHECKER = r"python3 scripts\check_solve_ipc.py"


def verify_solution(out_path, checker=CHECKER):
    cmd = f"{checker} {out_path}"
    sp = subprocess.run(cmd, capture_output=True, text=True)
    res = sp.stdout.strip().split("\n")
    if len(res) == 2:
        return res[0] == "OK", int(res[1])
    return False, 0


def run_boost(in_file, out_file):
    cmd = f"{boost} -t {BOOST_TIME} {in_file} {out_file}"
    code, output, stats = run(cmd, f"Running {cmd}...", timeout=BOOST_TIME + 10)
    return code, output, stats


def print_table(table, sep="  "):
    if not table or not table[0]:
        return
    num_cols = len(table[0])
    col_widths = [max(len(row[i]) for row in table) for i in range(num_cols)]
    for row in table:
        print(sep.join(row[i].ljust(col_widths[i]) for i in range(num_cols)))


if __name__ == "__main__":
    boost = TBoost.locate()[0][0]
    os.makedirs(OUT_BASE, exist_ok=True)

    table = []
    solutions = [i for i in sorted(os.listdir(BASE)) if i.endswith("1.txt")]  # for single noise_test_result
    # solutions = sorted(os.listdir(BASE))  # for all solutions

    for solution in solutions:
        in_file  = os.path.join(BASE, solution)
        out_file = os.path.join(OUT_BASE, solution)

        init_ok, init_score = verify_solution(in_file)
        if not init_ok:
            # table.append([solution, "INIT INVALID", "-", "-", "-", "-"])
            print(f"{solution} is not valid")
            continue

        code, output, stats = run_boost(in_file, out_file)
        boost_ok, boost_score = verify_solution(out_file)

        if not boost_ok:
            table.append([solution, f"BOOST INVALID (exit {code})", str(init_score), "-", "-", "-"])
            continue

        delta = boost_score - init_score
        pct_s = f"{delta / init_score * 100:+.1f}%" if init_score else "-"
        table.append([solution, "OK", str(init_score), str(boost_score), f"{delta:+d}", pct_s])

    table.sort(key=lambda x: -int(x[2]))
    table = [["solution", "status", "init_score", "boost_score", "delta", "delta_%"]] + table
    print_table(table)

