import itertools
import subprocess
import sys
import threading
import time

import psutil


def _kill_tree(pid):
    try:
        parent = psutil.Process(pid)
        procs = parent.children(recursive=True) + [parent]  # collect before killing
    except psutil.NoSuchProcess:
        return
    for p in procs:
        try:
            p.kill()
        except psutil.NoSuchProcess:
            pass


def run(command, label="Running...", show_output=False, cwd=None, timeout=30):
    t0 = time.perf_counter()

    process = subprocess.Popen(
        command,
        shell=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="cp866",
        errors="replace",
        cwd=cwd,
    )

    # Drain output in the background so the child never blocks on a full pipe
    chunks = []
    reader = threading.Thread(
        target=lambda: chunks.extend(iter(process.stdout.readline, "")),
        daemon=True,
    )
    reader.start()

    ncpu = psutil.cpu_count() or 1
    tracked = {}  # pid -> psutil.Process (must persist for cpu_percent deltas)
    cpu, ram = [], []
    timed_out = False
    spinner = itertools.cycle("|/-\\")

    def sample():
        try:
            root = psutil.Process(process.pid)
            alive = [root] + root.children(recursive=True)
        except psutil.NoSuchProcess:
            return
        total_cpu = total_ram = 0.0
        primed_only = True
        for p in alive:
            if p.pid not in tracked:
                tracked[p.pid] = p
                try:
                    p.cpu_percent(None)  # prime: first call always returns 0.0
                except psutil.Error:
                    pass
                continue
            primed_only = False
            try:
                total_cpu += tracked[p.pid].cpu_percent(None)
                total_ram += tracked[p.pid].memory_info().rss
            except psutil.Error:
                pass
        if not primed_only:
            cpu.append(total_cpu / ncpu)
            ram.append(total_ram / 1024 / 1024)

    try:
        while True:
            remaining = None if timeout is None else timeout - (time.perf_counter() - t0)
            if remaining is not None and remaining <= 0:
                timed_out = True
                _kill_tree(process.pid)
                break
            try:
                # Returns instantly when the process exits; otherwise ticks every 0.1s
                process.wait(timeout=0.1 if remaining is None else min(0.1, remaining))
                break
            except subprocess.TimeoutExpired:
                pass

            sample()
            sys.stdout.write(f"\r{label} {next(spinner)}")
            sys.stdout.flush()

        process.wait()
        reader.join(timeout=1)
    finally:
        t1 = time.perf_counter()

    output = "".join(chunks)

    stats = {
        "cpu_avg": sum(cpu) / len(cpu) if cpu else 0,
        "cpu_peak": max(cpu, default=0),
        "ram_avg": sum(ram) / len(ram) if ram else 0,
        "ram_peak": max(ram, default=0),
        "time": t1 - t0,
    }

    status = "timeout" if timed_out else ("ok" if process.returncode == 0 else "fail")

    print(
        f"\r{label} {status} ({stats['time']:.1f}s) | "
        f"CPU: {stats['cpu_avg']:.1f}% avg, {stats['cpu_peak']:.1f}% peak | "
        f"RAM: {stats['ram_avg']:.1f}/{stats['ram_peak']:.1f} MB avg/peak"
    )

    if show_output:
        print(output, end="")

    return process.returncode, output, stats