import os
import subprocess
from utils.run_process import run


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
    @staticmethod
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

    @staticmethod
    def locate():
        base = norm_path(r"named_subprojects\juzoou")
        paths = ["p15_lns.exe"]
        return [(os.path.join(base, p), p.replace(".exe", "").replace("p15_", "juzoou_")) for p in paths]


class Sermer:
    @staticmethod
    def build():
        cmd = "python scripts/build_cpp.py"
        run(cmd, "Building sermer...", cwd=norm_path(r"named_subprojects\sermer"))

    @staticmethod
    def locate():
        path = norm_path(r"named_subprojects\sermer\scripts\solve_cpp.py")
        info = subprocess.run(["python3", path], text=True, capture_output=True).stderr
        algos_line = info.split("--algorithm ")[1].split("]")[0]
        algos = algos_line.split("|")
        return [(f"python3 {path} --algorithm {a}", f"sermer_{a}") for a in algos]


class Tribouchet:
    @staticmethod
    def build():
        cmd = "python build.py"
        run(cmd, "Building tribouchet...", cwd=norm_path(r"named_subprojects\tribouchet"))

    @staticmethod
    def locate():
        path = norm_path(r"named_subprojects\tribouchet\solver.exe")
        depth_selection = [8, 10, 12]
        time_selection = [50]
        beam_selection = [8, 10, 14]
        params = [(d, t, b) for d in depth_selection for t in time_selection for b in beam_selection]

        return [(f"{path} --depth {d} --time {t} --beam {b}", f"tribouchet_d{d}_t{t}_b{b}") for d, t, b in params]
    


def locate_solvers(pool="tjas", ignore=""):
    lookup = {
        "a": Alexb,
        "j": Juzoou,
        "s": Sermer,
        "t": Tribouchet
    }
    res = []
    for i in pool:
        if (i in lookup) and (i not in ignore):
            res += lookup[i].locate()
    return res

def print_solvers():
    for cmd, label in locate_solvers():
        print(f"{(label + ':').ljust(25)} {cmd}")



if __name__ == "__main__":
    from utils.ensure_cwd import assert_cwd
    assert_cwd()

    Alexb.build()
    Juzoou.build()
    Sermer.build()
    Tribouchet.build()
    print_solvers()


    