#!/usr/bin/env python3
"""Test the actual UVC SDK adapter with fake register/EP0 callbacks, no hardware."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',type=Path)
    parser.add_argument('--cmake')
    args=parser.parse_args()
    root=Path(__file__).resolve().parent
    build=args.build_dir or root/'build'/'adapter-host'
    cmake=args.cmake or shutil.which('cmake')
    if not cmake:
        candidate=Path('C:/Program Files/CMake/bin/cmake.exe')
        if candidate.is_file():cmake=str(candidate)
    if not cmake:parser.error('CMake unavailable; use --cmake PATH')
    configure=[cmake,'-S',str(root/'tests'/'adapter'),'-B',str(build)]
    if sys.platform=='win32':configure+=['-G','Visual Studio 17 2022','-A','x64']
    subprocess.run(configure,check=True)
    subprocess.run([cmake,'--build',str(build),'--config','Release'],check=True)
    ctest=Path(cmake).resolve().with_name('ctest.exe' if sys.platform=='win32' else 'ctest')
    if not ctest.is_file():
        found=shutil.which('ctest')
        if not found:parser.error('CTest unavailable')
        ctest=Path(found)
    subprocess.run([str(ctest),'--test-dir',str(build),'-C','Release','--output-on-failure'],check=True)

if __name__=='__main__':main()
