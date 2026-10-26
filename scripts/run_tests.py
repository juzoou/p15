"""Build once and run regressions, archived integration suites and teacher tests."""
import subprocess
import sys
try:
    from .build_cpp import ROOT, build_solver
except ImportError:
    from build_cpp import ROOT, build_solver


def main():
    build_solver()
    commands = [[sys.executable, "-m", "unittest", "discover", "-s", "tests", "-p", "test_*.py", "-v"]]
    commands.extend([sys.executable, str(path.relative_to(ROOT))]
                    for path in sorted((ROOT / "cpp" / "tests").glob("test_*.py")))
    commands.append([sys.executable, "scripts/run_teacher_tests.py"])
    failures = 0
    for command in commands:
        print("Running:", " ".join(command), flush=True)
        failures += subprocess.run(command, cwd=ROOT).returncode != 0
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
