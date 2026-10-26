"""Repository solver interface: options followed by INPUT OUTPUT; stdout is F."""
import subprocess
import sys

try:
    from .build_cpp import build_solver
except ImportError:
    from build_cpp import build_solver


def main() -> int:
    try:
        binary = build_solver()
        return subprocess.run([str(binary), "--score-only", *sys.argv[1:]]).returncode
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
