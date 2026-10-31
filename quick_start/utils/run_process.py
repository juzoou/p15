import subprocess
import itertools
import sys
import time

def run(command, label="Running...", show_output=False, cwd=None):
    process = subprocess.Popen(
        command,
        shell=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="cp866",
        cwd=cwd
    )

    output = []

    for char in itertools.cycle("|/-\\"):
        if process.poll() is not None:
            break

        sys.stdout.write(f"\r{label} {char}")
        sys.stdout.flush()
        time.sleep(0.1)

    output, _ = process.communicate()

    print(f"\r{label} Done ({'ok' if process.returncode == 0 else 'fail'})")
    if show_output:
        print(output, end="")

    return process.returncode, output

