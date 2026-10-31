"""Use the repository build on Windows and POSIX, without repeated compilation."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from scripts.build_cpp import build_solver


def binary_path():
    return build_solver()
