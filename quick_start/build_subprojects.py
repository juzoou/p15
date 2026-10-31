import os
import subprocess
from utils.run_process import run
RepoName = "p15"


def norm_path(p):
    return p.replace("\\", os.path.sep)


class Alexb:
    @staticmethod
    def build():
        if os.name == "nt":
            path = norm_path(r"named_subprojects\alexb\build.bat")
        else:
            path = norm_path(r"named_subprojects\alexb\build.sh")
        run(f"powershell {path}", "Building alexb...", cwd=norm_path(r"named_subprojects\alexb"))

    @staticmethod
    def locate():
        base = norm_path(r"named_subprojects\alexb\bin")
        paths = os.listdir(base)
        return [(os.path.join(base, p), p.replace(".exe", "").replace("p15_", "alexb_")) for p in paths]


class Juzoou:
    def build():
        path = norm_path(r"named_subprojects\juzoou\build.sh")
        with open(path) as f:
            for cmd in f:
                if cmd.startswith("g++ "):
                    break
            else:
                print("Could not find compile line")
                return
        run(cmd, "Building juzoou...", cwd=norm_path(r"named_subprojects\juzoou"))

    def locate():
        base = norm_path(r"named_subprojects\juzoou")
        paths = ["p15_lns.exe"]
        return [(os.path.join(base, p), p.replace(".exe", "").replace("p15_", "juzoou_")) for p in paths]


class Sermer:
    def build():
        cmd = "python scripts/build_cpp.py"
        run(cmd, "Building sermer...", cwd=norm_path(r"named_subprojects\sermer"))

    def locate():
        path = norm_path(r"named_subprojects\sermer\scripts\solve_cpp.py")
        info = subprocess.run(["python3", path], text=True, capture_output=True).stderr
        algos_line = info.split("--algorithm ")[1].split("]")[0]
        algos = algos_line.split("|")
        return [(f"python3 {path} --algorithm {a}", f"sermer_{a}") for a in algos]
    

def locate_solvers():
    return Alexb.locate() + Juzoou.locate() + Sermer.locate()

def print_solvers():
    for cmd, label in locate_solvers():
        print(f"{(label + ':').ljust(25)} {cmd}")



if __name__ == "__main__":
    cwd = os.getcwd()
    if os.path.basename(cwd) != RepoName:
        print(f"Script should be ran from repository root ({RepoName})")
        print(f"Current working directory: {cwd}")
        exit(1)

    # Alexb.build()
    # Juzoou.build()
    # Sermer.build()
    print_solvers()


    