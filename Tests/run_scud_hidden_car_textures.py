#!/usr/bin/env python3
"""ROM-free regression test for the Scud Race Plus secret-vehicle texture overlay builder."""
import argparse
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');a=p.parse_args()
    binary=ROOT/'build/scud-hidden-car-textures';binary.parent.mkdir(exist_ok=True)
    cmd=['g++','-std=c++17','-O1','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-ISrc','Tests/ScudHiddenCarTexturesTest.cpp','Src/Network/Games/Scud/HiddenCarTextures.cpp','-o',str(binary)]
    if a.sanitize:cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
    subprocess.run(cmd,cwd=ROOT,check=True);subprocess.run([str(binary)],check=True)
