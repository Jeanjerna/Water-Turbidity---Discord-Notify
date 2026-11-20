"""Compile and run the portable firmware logic on the host (no board, no network)."""
import os
import shutil
import subprocess
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
out = root / ".pio" / "host-tests"
out.mkdir(parents=True, exist_ok=True)

compiler = shutil.which("g++") or shutil.which("clang++")
if not compiler:
    sys.exit("g++ or clang++ is required to run the host tests.")

exe = out / ("monitor_tests.exe" if os.name == "nt" else "monitor_tests")
subprocess.run(
    [compiler, "-std=c++17", "-Wall", "-Wextra",
     "-I" + str(root / "include"),
     str(root / "tests" / "monitor_tests.cpp"),
     "-o", str(exe)],
    cwd=root, check=True)
subprocess.run([str(exe)], cwd=root, check=True)
