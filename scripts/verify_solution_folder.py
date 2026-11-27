import os
import subprocess
import sys

# Expecting: python script.py <checker> <reference> <outfile>
if len(sys.argv) < 3:
    print("Usage: python script.py <checker> <reference_dir> <outfile>")
    print("Appropriate checker is at /scripts/check_solve.ipc.py")
    sys.exit(1)

checker = sys.argv[1]
reference = sys.argv[2]
outfile = None if len(sys.argv) < 4 else sys.argv[3]

out = ""
for file in os.listdir(reference):
    path = os.path.join(reference, file)
    sp = subprocess.run([sys.executable, checker, path], capture_output=True, text=True)

    ok = sp.returncode == 0
    msg = "\n".join([sp.stdout, sp.stdout])
    out += f"# {file}\n{sp.msg}\n"

if outfile is not None:
    with open(outfile, "w", encoding="utf-8") as f:
        f.write(out)
else:
    print("Outfile not specified, output in stdout only\n\n\n")

print(out)
