import subprocess
import os
path = os.path.dirname(os.path.abspath(__file__))
subprocess.run(["g++", "-O3", "-march=native", "-std=c++17", "-pthread", "optimize.cpp", "-o", "optimize"], check=True, cwd=path)
