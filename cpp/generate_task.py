"""Compatibility entry point for the shared repository generator."""
from pathlib import Path
import runpy

if __name__ == "__main__":
    runpy.run_path(str(Path(__file__).resolve().parents[1] / "generate_task.py"), run_name="__main__")
