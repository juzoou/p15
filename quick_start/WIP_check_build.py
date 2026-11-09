from build_subprojects import locate_solvers
# from utils.run_process import run
from utils.measure_process import run
from utils.ensure_cwd import assert_cwd
assert_cwd()

TIMEOUT = 120

res = []
for cmd, label in locate_solvers("t"):
    in_path = r"tests\task89x98.txt"
    out_path = rf"test_solutions\{label}.txt"
    with open(out_path, "w"): pass

    full_cmd = f"{cmd} {in_path} {out_path}"
    code, output, stat = run(full_cmd, f"Running '{full_cmd}'...", timeout=TIMEOUT)
    try:
        score = int(output.strip().split("\n")[-1])
    except Exception as e:
        score = 0
    res.append((label, code, output, score, stat))

res.sort(key=lambda r: -r[3])
for label, code, output, score, stat in res:
    ok = code == 0
    ok_line = "ok  " if ok else "fail"
    newline = "\n"
    print(f"{label.ljust(21)}\t{stat['time']:.1f}\t{ok_line}\t{output.replace(newline, '').strip()}")
