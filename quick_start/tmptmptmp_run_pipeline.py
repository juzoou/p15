import time
from typing import Callable
import psutil
from dataclasses import dataclass




import os
import sys
import threading

RESET, DIM, BOLD = "\033[0m", "\033[2m", "\033[1m"
GREEN, RED = "\033[32m", "\033[31m"   # normal intensity (was bright 92 / 91)
TRACK_COLOR = DIM                     # same style as the time column


class TrackLogger:
    """Each CPU core is a column ("track"). A process occupies a track from its
    start record (┬) to its end record (┴); rows in between show │."""

    def __init__(self, n_tracks: int, cell_width: int = 20):
        self.w = cell_width
        self.tracks: list[object | None] = [None] * n_tracks
        self.where: dict[object, int] = {}
        self.t0 = time.perf_counter()
        self._lock = threading.Lock()

        self.color = sys.stdout.isatty() and "NO_COLOR" not in os.environ
        if self.color and os.name == "nt":
            os.system("")  # enables ANSI escape processing in the Windows console

        self._header()

    # ---------- formatting ----------
    def _paint(self, text: str, *styles: str) -> str:
        styles = [s for s in styles if s]
        if not self.color or not text or not styles:
            return text
        return "".join(styles) + text + RESET

    def _start_cell(self, label: str) -> str:
        """'┬' in the track style, label in normal text. Padding uses plain length."""
        text = f"┬ {label}"
        if len(text) > self.w:
            text = text[: self.w - 1] + "…"
        return self._paint(text[0], TRACK_COLOR) + text[1:] + " " * (self.w - len(text))

    def _cell(self, text: str, *styles: str) -> str:
        """Truncate/pad using the PLAIN text length, then color only the text."""
        if len(text) > self.w:
            text = text[: self.w - 1] + "…"
        return self._paint(text, *styles) + " " * (self.w - len(text))

    def _print(self, cells: list[str]) -> None:
        elapsed = self._paint(f"{time.perf_counter() - self.t0:.2f}s".rjust(9), DIM)
        print(f"{elapsed}  " + "  ".join(cells))

    def _header(self) -> None:
        cells = [self._cell(f"core {i}", BOLD) for i in range(len(self.tracks))]
        print(self._paint("time".rjust(9), BOLD) + "  " + "  ".join(cells))

    def _base(self) -> list[str]:
        return [
            self._cell("│", TRACK_COLOR) if k is not None else self._cell("")
            for k in self.tracks
        ]

    # ---------- events ----------
    def start(self, key, label: str) -> None:
        with self._lock:
            idx = next((i for i, k in enumerate(self.tracks) if k is None), None)
            if idx is None:  # more processes than tracks: add one
                self.tracks.append(None)
                idx = len(self.tracks) - 1
                self._header()
            cells = self._base()
            cells[idx] = self._start_cell(label)
            self.tracks[idx] = key
            self.where[key] = idx
            self._print(cells)

    def end(self, key, status: str, ok: bool) -> None:
        with self._lock:
            idx = self.where.pop(key, None)
            if idx is None:
                return
            cells = self._base()
            cells[idx] = self._cell(f"┴ {status}", DIM, GREEN if ok else RED)
            self.tracks[idx] = None
            self._print(cells)






@dataclass(frozen=True)
class Command:
    label: str
    command: str
    cwd: str | None = None


class ProcessGroup:
    @staticmethod
    def _open(cmd: Command):
        process = psutil.Popen(
            cmd.command,
            shell=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            cwd=cmd.cwd
        )
        return process

    def __init__(self, cmd: Command):
        self.cmd = cmd
        self.main: subprocess.Popen | psutil.Popen = self._open(cmd)
        self.track_start_time = time.perf_counter()
        self._tracked_procs: dict[tuple[int, float], psutil.Process] = {}
        self._sync_tree()  # Initialize CPU baselines immediately

    def _sync_tree(self) -> list[psutil.Process]:
        """Updates the cached process tree and returns [children..., main]."""
        try:
            current_procs = self.main.children(recursive=True)[::-1] + [self.main]
        except psutil.NoSuchProcess:
            self._tracked_procs.clear()
            return []

        active_keys = set()
        ordered_cached_procs = []

        for p in current_procs:
            try:
                key = (p.pid, p.create_time())
                active_keys.add(key)
                if key not in self._tracked_procs:
                    p.cpu_percent(interval=None)
                    self._tracked_procs[key] = p
                ordered_cached_procs.append(self._tracked_procs[key])
            except (psutil.NoSuchProcess, psutil.AccessDenied):
                pass

        for key in list(self._tracked_procs.keys()):
            if key not in active_keys:
                del self._tracked_procs[key]
        return ordered_cached_procs

    def get_tree(self) -> list[psutil.Process]:
        return self._sync_tree()

    def kill(self, timeout: float = 0.3):
        procs = self.get_tree()
        for p in procs:
            try:
                p.kill()
            except psutil.NoSuchProcess:
                pass
        psutil.wait_procs(procs, timeout=timeout)

    def measure_ram(self) -> float:
        """In MB"""
        total_ram_bytes = 0
        for p in self.get_tree():
            try:
                total_ram_bytes += p.memory_info().rss
            except (psutil.NoSuchProcess, psutil.AccessDenied):
                pass
        return total_ram_bytes / 1024 / 1024

    def measure_cpu_cores(self) -> float:
        """In cores (float)"""
        total_percent = 0.0
        for p in self.get_tree():
            try:
                total_percent += p.cpu_percent(interval=None)
            except (psutil.NoSuchProcess, psutil.AccessDenied):
                pass
        return total_percent / 100.0

    def measure_time(self) -> float:
        """In Seconds"""
        return time.perf_counter() - self.track_start_time


import subprocess
from threading import Thread
from queue import Queue, Empty


class ProcessPool:
    refresh_delay_s = 0.5
    subprocess_timeout = 60

    ram_budget_percentile = 0.9
    hold_before_ram_kill_s = 4

    def __init__(self):
        self.memory_overflow_start = None

        self.max_process_count = psutil.cpu_count(logical=False)
        self.cpu_budget = psutil.cpu_count(logical=True)
        self.cpu_budget_per_process = self.cpu_budget / self.max_process_count

        self.ram_budget_mb = psutil.virtual_memory().available * self.ram_budget_percentile / 1024 / 1024
        self.ram_budget_per_process = self.ram_budget_mb / self.max_process_count

        self.main_queue: list[Command] = []
        self.output_queue: Queue[tuple[Command, int, str]] = Queue()

        self.running: list[ProcessGroup] = []
        self.bad_commands: list[tuple[Command, str]] = []
        self.thread = None
        self.log = None

    def collect_ram_usage(self):
        return {group.cmd: group.measure_ram() for group in self.running}

    def collect_cpu_usage(self):
        return {group.cmd: group.measure_cpu_cores() for group in self.running}

    def _w_can_start_process(self, current_ram, current_cpu):
        if len(self.running) == self.max_process_count:
            return False
        if current_cpu > self.cpu_budget - self.cpu_budget_per_process:
            return False
        if current_ram > self.ram_budget_mb - self.ram_budget_per_process:
            return False
        return True

    def _w_kill_group(self, group: ProcessGroup, reason):
        group.kill()
        self.log.end(group, f"killed: {reason}", ok=False)
        self.bad_commands.append((group.cmd, reason))
        self.running.remove(group)

    def _w_contain_memory(self, ram_usage):
        current_ram = sum(ram_usage.values())
        if current_ram > self.ram_budget_mb:
            if self.memory_overflow_start is None:
                self.memory_overflow_start = time.perf_counter()
            elif time.perf_counter() - self.memory_overflow_start > self.hold_before_ram_kill_s:
                heaviest_group = max(self.running, key=lambda x: ram_usage[x.cmd])
                self._w_kill_group(heaviest_group, f"ram overflow {ram_usage[heaviest_group.cmd]}")
        else:
            self.memory_overflow_start = None

    def _w_open_process(self):
        if not self.main_queue:
            return False
        next_cmd = self.main_queue.pop(0)
        new_process = ProcessGroup(next_cmd)
        self.log.start(new_process, next_cmd.label)
        self.running.append(new_process)
        return True

    def _w_contain_time(self):
        kill_list = []
        for group in self.running:
            if group.measure_time() > self.subprocess_timeout:
                kill_list.append(group)
        for group in kill_list:
            self._w_kill_group(group, "timeout")

    def _w_collect_finished(self):
        group_by_process = {group.main: group for group in self.running}
        gone_processes, running_processes = psutil.wait_procs(group_by_process.keys(), 0)
        self.running = [group_by_process[p] for p in running_processes]

        p: subprocess.Popen | psutil.Popen
        for p in gone_processes:
            group = group_by_process[p]
            self.log.end(group, f"done ({p.returncode})", ok=(p.returncode == 0))
            code = p.returncode
            out = p.stdout.read()
            self.output_queue.put((group.cmd, code, out))

    def worker_step(self):
        self._w_collect_finished()
        
        cpu_usage, ram_usage = self.collect_cpu_usage(), self.collect_ram_usage()
        current_cpu, current_ram = sum(cpu_usage.values()), sum(ram_usage.values())
        self._w_contain_memory(ram_usage)
        self._w_contain_time()

        if self._w_can_start_process(current_ram, current_cpu):
            self._w_open_process()

    def worker(self):
        while True:
            if (not self.running) and (not self.main_queue):
                break
            self.worker_step()
            time.sleep(self.refresh_delay_s)

        # TODO: finish "ram overflow" commands - execute 1 by 1, with timeout


    def open_pool(self, commands: list[Command]):
        width = max([len(c.label) for c in commands] + [12]) + 2
        self.log = TrackLogger(self.max_process_count, width)
        self.main_queue = commands.copy()
        self.thread = Thread(target=self.worker, daemon=True)
        self.thread.start()


if __name__ == "__main__":
    from utils.ensure_cwd import assert_cwd
    assert_cwd()
    from build_subprojects import locate_solvers
    import os
    task = r"C:\Users\User\Desktop\Учеба\Лабы\OPT\p15\tests\task89x98.txt"
    outdir = "test_solutions"
    os.makedirs(outdir, exist_ok=True)

    commands = []
    for solver, label in locate_solvers():
        cmd = f"{solver} {task} {os.path.join(outdir, label)}.txt"
        commands.append(Command(label, cmd))

    pool = ProcessPool()
    pool.open_pool(commands)
    
    # Wait for the background worker thread to finish all commands
    # pool.thread.join()
    try:
        while pool.thread.is_alive():
            time.sleep(1)
    except KeyboardInterrupt:
        pass

    print()
    # Read finished results
    while not pool.output_queue.empty():
        cmd, return_code, output = pool.output_queue.get()
        print(f"[{cmd.label}] code={return_code} | out={output.strip()}")

    # Check killed commands
    for cmd, reason in pool.bad_commands:
        print(f"[KILLED] {cmd.label}: {reason}")


        


