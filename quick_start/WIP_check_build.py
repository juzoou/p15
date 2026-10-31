from build_subprojects import locate_solvers
from utils.run_process import run

res = []
for cmd, label in locate_solvers():
    in_path = r"tests\task5x4.txt"
    out_path = rf"test_solutions\{label}.txt"
    with open(out_path, "w"): pass

    full_cmd = f"{cmd} {in_path} {out_path}"
    code, output = run(full_cmd, f"Running '{full_cmd}'...")
    res.append((label, code, output))

print()
for label, code, output in res:
    ok = code == 0
    ok_line = "ok  " if ok else "fail"
    newline = "\n"
    print(f"{label.ljust(25)}\t{ok_line}\t{output.replace(newline, '').strip()}")
