"""Regression and independent optimality checks for the integrated solver."""
import heapq
import itertools
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from generate_task import make_scramble, render_task
from scripts.build_cpp import build_solver
from scripts.parse_vasekin import _parse_input, read_input_text
from scripts.check_solve import _check_order, check_if_solve, is_solvable


class SolverTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.binary = build_solver()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.input = self.directory / "input.txt"
        self.output = self.directory / "output.txt"

    def task(self, rows=2, cols=3, board=(1, 2, 3, 4, 0, 5), costs=(1, 1, 1, 1), order=None):
        text = render_task(rows, cols, list(board), costs, 42, 0)
        if order is not None:
            text += "ORDER\n" + "\n".join(f"{t} {d}" for t, d in order) + "\n/\n"
        self.input.write_text(text, encoding="utf-8")

    def invoke(self, *options, expected=0):
        process = subprocess.run([str(self.binary), str(self.input), str(self.output), *options],
                                 capture_output=True, encoding="utf-8", errors="replace", timeout=15)
        self.assertEqual(process.returncode, expected, process.stdout + process.stderr)
        return process

    def checked(self):
        return _check_order(_parse_input(read_input_text(self.output)))

    def test_default_auto(self):
        self.task()
        self.assertIn("algorithm=ASTAR", self.invoke().stdout)
        self.assertEqual(self.checked().score, 499)

    def test_partial_order_score(self):
        self.task(order=[])
        output = self.invoke().stdout
        self.assertIn("F=400", output)
        self.assertIn("solved=false", output)
        self.assertFalse(self.checked().solved)

    def test_one_dimensional_all_algorithms(self):
        algorithms = ("auto", "rowbyrow", "columnbycolumn", "astar", "wastar", "dijkstra",
                      "bidijkstra", "idastar", "01bfs", "dial", "linearconflict", "rbfs", "pdb")
        for rows, cols in [(1, 4), (4, 1)]:
            for algo in algorithms:
                with self.subTest(shape=(rows, cols), algo=algo):
                    self.task(rows, cols, (1, 0, 2, 3))
                    self.invoke("--algorithm", algo)
                    checked = self.checked()
                    self.assertTrue(checked.solved)
                    self.assertEqual(checked.total_cost, 2)

    def test_one_dimensional_reachability(self):
        for board in itertools.permutations(range(4)):
            self.task(1, 4, board)
            expected = tuple(x for x in board if x) == (1, 2, 3)
            self.assertEqual(check_if_solve(self.input), expected)
            self.invoke(expected=0 if expected else 2)

    def test_already_solved_constructive(self):
        for rows, cols in [(2, 3), (3, 2), (3, 3), (10, 15)]:
            for algo in ["rowbyrow", "columnbycolumn"]:
                self.task(rows, cols, tuple(range(1, rows * cols)) + (0,))
                self.invoke("--algorithm", algo)
                self.assertEqual(self.checked().valid_moves, 0)

    def test_simplified_and_options_before_paths(self):
        self.input.write_text("2 3 4 5 3 2 1 2 3 4 0 5", encoding="utf-8")
        process = subprocess.run([str(self.binary), "--algorithm", "astar", "--score-only",
                                  str(self.input), str(self.output)], capture_output=True, text=True, timeout=15)
        self.assertEqual(process.returncode, 0, process.stderr)
        self.assertEqual(process.stdout.strip(), "496")
        self.assertEqual(self.checked().score, 496)

    def test_wrapper(self):
        self.task()
        process = subprocess.run([sys.executable, str(ROOT / "scripts" / "solve_cpp.py"),
                                  "--algorithm", "astar", str(self.input), str(self.output)],
                                 capture_output=True, text=True, timeout=15)
        self.assertEqual(process.returncode, 0, process.stderr)
        self.assertEqual(process.stdout.strip(), "499")

    def test_utf8_bom_and_unicode_paths(self):
        self.task()
        data = self.input.read_text(encoding="utf-8")
        self.input = self.directory / "задача.txt"
        self.output = self.directory / "решение.txt"
        self.input.write_text(data, encoding="utf-8-sig")
        self.invoke()
        self.assertTrue(self.checked().solved)

    def test_input_never_overwritten(self):
        self.task()
        before = self.input.read_bytes()
        self.output = self.input
        self.assertIn("different files", self.invoke(expected=2).stderr)
        self.assertEqual(before, self.input.read_bytes())

    def test_output_preflight(self):
        self.task()
        self.output = self.directory / "missing" / "result.txt"
        self.assertIn("cannot open output", self.invoke("--max-expanded", "1", expected=2).stderr)

    def test_invalid_weight_and_algorithm(self):
        self.task()
        for weight in ["0.5", "nan", "inf", "2junk"]:
            self.assertIn("weight", self.invoke("--weight", weight, expected=2).stderr)
        self.assertIn("unknown algorithm", self.invoke("--algorithm", "missing", expected=2).stderr)

    def test_limits(self):
        self.task(2, 3, (4, 1, 2, 5, 3, 0))
        self.assertIn("limit", self.invoke("--max-expanded", "1", expected=2).stderr)
        self.task(3, 4, tuple(range(1, 12)) + (0,))
        self.assertIn("at most 9", self.invoke("--algorithm", "pdb", expected=2).stderr)
        self.task(costs=(2, 1, 1, 1))
        self.assertIn("0 or 1", self.invoke("--algorithm", "01bfs", expected=2).stderr)

    def test_advanced_cost_overflow(self):
        self.task(2, 3, (4, 1, 2, 5, 3, 0), (3_000_000_000_000_000_000,) * 4)
        for algo in ["astar", "linearconflict", "rbfs", "annealing"]:
            self.assertIn("overflow", self.invoke("--algorithm", algo, expected=2).stderr)

    def test_generator_one_dimensional_and_square(self):
        for rows, cols in [(1, 5), (5, 1), (2, 2), (3, 3)]:
            board, steps = make_scramble(rows, cols, 3, random.Random(42))
            self.assertGreaterEqual(steps, 3)
            self.task(rows, cols, board)
            self.assertTrue(check_if_solve(self.input))
            self.assertNotEqual(board, list(range(1, rows * cols)) + [0])
            self.invoke()
            self.assertTrue(self.checked().solved)

    def test_existing_archived_solutions(self):
        count = 0
        for archive in ["5x4_test.zip", "89x98_test.zip"]:
            with zipfile.ZipFile(ROOT / "tests" / archive) as zipped:
                for name in zipped.namelist():
                    text = zipped.read(name).decode("utf-8-sig")
                    if "ORDER" not in text:
                        continue
                    self.input.write_text(text, encoding="utf-8")
                    process = self.invoke()
                    expected = _check_order(_parse_input(text))
                    self.assertIn(f"F={expected.score}", process.stdout)
                    self.assertTrue(self.checked().solved)
                    count += 1
        self.assertEqual(count, 12)

    def test_exact_algorithms_against_independent_distances(self):
        def neighbors(board):
            blank = board.index(0)
            row, col = divmod(blank, 3)
            for destination, direction, legal in [(blank + 3, 0, row < 1), (blank - 3, 1, row > 0),
                                                  (blank + 1, 2, col < 2), (blank - 1, 3, col > 0)]:
                if legal:
                    changed = list(board)
                    changed[blank], changed[destination] = changed[destination], changed[blank]
                    yield tuple(changed), direction

        count = 0
        for costs in [(1, 1, 1, 1), (2, 3, 1, 4), (0, 1, 0, 1), (0, 0, 0, 0)]:
            goal = (1, 2, 3, 4, 5, 0)
            distances, queue = {goal: 0}, [(0, goal)]
            while queue:
                cost, board = heapq.heappop(queue)
                if distances[board] != cost:
                    continue
                for neighbor, direction in neighbors(board):
                    candidate = cost + costs[direction ^ 1]
                    if candidate < distances.get(neighbor, float("inf")):
                        distances[neighbor] = candidate
                        heapq.heappush(queue, (candidate, neighbor))
            algorithms = ["astar", "wastar", "dijkstra", "bidijkstra", "idastar", "linearconflict", "rbfs", "dial"]
            if max(costs) <= 1:
                algorithms.append("01bfs")
            if costs == (1, 1, 1, 1):
                algorithms.append("pdb")
            for board in random.Random(42).sample(list(distances), 8):
                for algo in algorithms:
                    with self.subTest(board=board, costs=costs, algorithm=algo):
                        self.task(2, 3, board, costs)
                        self.invoke("--algorithm", algo, "--weight", "1", "--max-expanded", "100000")
                        checked = self.checked()
                        self.assertTrue(checked.solved)
                        self.assertEqual(checked.total_cost, distances[board])
                        count += 1
        self.assertEqual(count, 288)


if __name__ == "__main__":
    unittest.main()
