#!/usr/bin/env python3
"""Build and run portable UVC stream/control tests without contacting hardware."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--cmake", help="CMake executable path")
    args = parser.parse_args()
    source = Path(__file__).resolve().parent
    build = args.build_dir or source / "build" / "host"
    cmake = args.cmake or shutil.which("cmake")
    if not cmake and sys.platform == "win32":
        candidate = Path("C:/Program Files/CMake/bin/cmake.exe")
        if candidate.is_file():
            cmake = str(candidate)
    if not cmake:
        parser.error("CMake was not found; use --cmake PATH")
    configure = [cmake, "-S", str(source), "-B", str(build)]
    if sys.platform == "win32":
        configure += ["-G", "Visual Studio 17 2022", "-A", "x64"]
    subprocess.run(configure, check=True)
    subprocess.run([cmake, "--build", str(build), "--config", "Release"], check=True)
    ctest = Path(cmake).resolve().with_name("ctest.exe" if sys.platform == "win32"
                                             else "ctest")
    if not ctest.is_file():
        resolved = shutil.which("ctest")
        if not resolved:
            parser.error("CTest was not found alongside CMake or on PATH")
        ctest = Path(resolved)
    subprocess.run([str(ctest), "--test-dir", str(build), "-C", "Release",
                    "--output-on-failure"], check=True)


if __name__ == "__main__":
    main()
