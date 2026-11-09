"""Build script for the sliding puzzle solver (Linux and Windows, standard library only).

    python build.py              release build  -> ./solver  (./solver.exe on Windows)
    python build.py --debug      no optimisation, debug info, extra self-checks (-DSP_VERIFY)
    python build.py --native     add -march=native (g++/clang++ only; not portable)
    python build.py --clean      remove build outputs

The compiler is taken from $CXX if set, otherwise tried in this order: g++, clang++, MSVC cl.exe.
"""
import argparse
import os
import pathlib
import shlex
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent
SRC_DIR = ROOT / "src"
BUILD_DIR = ROOT / "build"
IS_WINDOWS = os.name == "nt"
EXE = ROOT / ("solver.exe" if IS_WINDOWS else "solver")


def die(msg):
    print("build failed: " + msg, file=sys.stderr)
    sys.exit(1)


def is_msvc(cmd0):
    return pathlib.Path(cmd0).stem.lower() == "cl"


def load_msvc_environment():
    """Best effort: locate Visual Studio with vswhere.exe and import its x64 build environment."""
    if not IS_WINDOWS:
        return False
    pf = os.environ.get("ProgramFiles(x86)") or os.environ.get("ProgramFiles")
    if not pf:
        return False
    vswhere = pathlib.Path(pf) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if not vswhere.exists():
        return False
    try:
        out = subprocess.run(
            [str(vswhere), "-latest", "-products", "*", "-requires",
             "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"],
            capture_output=True, text=True, check=True).stdout.strip()
        if not out:
            return False
        vcvars = pathlib.Path(out.splitlines()[0]) / "VC" / "Auxiliary" / "Build" / "vcvars64.bat"
        if not vcvars.exists():
            return False
        # vcvars64.bat only works inside cmd.exe, so run it there and import the resulting environment.
        res = subprocess.run('cmd /s /c ""%s" >nul && set"' % vcvars, capture_output=True, text=True)
        if res.returncode != 0:
            return False
        for line in res.stdout.splitlines():
            if "=" in line:
                k, v = line.split("=", 1)
                os.environ[k] = v
        return shutil.which("cl") is not None
    except (OSError, subprocess.SubprocessError):
        return False


def find_compiler():
    """Returns the compiler command as a list (first element is the executable)."""
    override = os.environ.get("CXX")
    if override:
        cmd = shlex.split(override, posix=not IS_WINDOWS)
        if not cmd or shutil.which(cmd[0]) is None:
            die("CXX is set to '%s' but that compiler was not found" % override)
        return cmd
    for name in ("g++", "clang++"):
        if shutil.which(name):
            return [name]
    if shutil.which("cl"):
        return ["cl"]
    if IS_WINDOWS and load_msvc_environment():
        return ["cl"]
    msg = "no C++ compiler found (tried g++, clang++, cl.exe). Install one or set CXX."
    if IS_WINDOWS:
        msg += ("\nFor MSVC, run this script from a 'Developer Command Prompt for VS' "
                "(or 'x64 Native Tools Command Prompt') so that cl.exe is on PATH.")
    die(msg)


def do_clean():
    if BUILD_DIR.exists():
        shutil.rmtree(BUILD_DIR, ignore_errors=True)
    for name in ("solver", "solver.exe"):
        p = ROOT / name
        if p.exists():
            p.unlink()
    print("cleaned")


def main():
    ap = argparse.ArgumentParser(description="Build the sliding puzzle solver.")
    ap.add_argument("--debug", action="store_true", help="no optimisation, debug info, self-checks enabled")
    ap.add_argument("--clean", action="store_true", help="remove build outputs and exit")
    ap.add_argument("--native", action="store_true", help="add -march=native (g++/clang++ only)")
    args = ap.parse_args()

    if args.clean:
        do_clean()
        return

    sources = sorted(SRC_DIR.rglob("*.cpp"))
    if not sources:
        die("no .cpp files found under %s" % SRC_DIR)

    cxx = find_compiler()
    if EXE.exists():
        EXE.unlink()

    # The compiler runs inside the project folder and gets relative, plain-ASCII paths only:
    # MinGW's ld (and other ANSI-codepage tools) cannot open absolute paths that contain
    # non-ASCII characters, e.g. a project folder with a non-English name.
    rel_sources = [p.relative_to(ROOT).as_posix() for p in sources]
    if is_msvc(cxx[0]):
        flags = ["/nologo", "/std:c++17", "/EHsc"]
        flags += ["/Od", "/Zi", "/DSP_VERIFY"] if args.debug else ["/O2"]
        if args.native:
            print("note: --native is ignored for MSVC")
        BUILD_DIR.mkdir(exist_ok=True)
        cmd = cxx + flags + ["/Fe../" + EXE.name] + ["../" + r for r in rel_sources]
        cwd = str(BUILD_DIR)  # .obj / .pdb files land in build/
    else:
        flags = ["-std=c++17", "-Wall", "-Wextra"]
        flags += ["-O0", "-g", "-DSP_VERIFY"] if args.debug else ["-O2"]
        if args.native:
            flags.append("-march=native")
        cmd = cxx + flags + ["-o", EXE.name] + rel_sources
        cwd = str(ROOT)

    print("compiler:", " ".join(cxx))
    try:
        res = subprocess.run(cmd, cwd=cwd)
    except OSError as e:
        die("could not run the compiler: %s" % e)
    if res.returncode != 0:
        die("compiler exited with code %d" % res.returncode)
    if not EXE.exists():
        die("compiler reported success but %s was not produced" % EXE)
    print("built", EXE)


if __name__ == "__main__":
    main()
