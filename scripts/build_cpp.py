"""Build the shared C++ solver with GCC or Clang; no third-party Python packages."""
from pathlib import Path
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def build_solver(force: bool = False) -> Path:
    binary = ROOT / "build" / ("pyatnashki_cpp.exe" if os.name == "nt" else "pyatnashki_cpp")
    sources = sorted((ROOT / "cpp" / "src").glob("*.cpp"))
    dependencies = sources + list((ROOT / "cpp" / "include").rglob("*.hpp")) + [Path(__file__)]
    if not force and binary.exists() and all(p.stat().st_mtime <= binary.stat().st_mtime for p in dependencies):
        return binary
    compiler = shutil.which("g++") or shutil.which("clang++")
    if not compiler:
        raise RuntimeError("Install g++ or clang++ and add it to PATH, or build with CMake.")
    binary.parent.mkdir(parents=True, exist_ok=True)
    # Relative source paths also work with MinGW installations lacking Unicode paths.
    subprocess.run([compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", "-pedantic",
                    "-Icpp/include", *(str(p.relative_to(ROOT)) for p in sources),
                    "-o", str(binary.relative_to(ROOT)),
                    *(["-lshell32"] if os.name == "nt" else [])], cwd=ROOT, check=True)
    return binary


if __name__ == "__main__":
    print(build_solver(force=True))
