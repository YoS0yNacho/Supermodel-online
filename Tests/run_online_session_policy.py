#!/usr/bin/env python3
"""ROM-free online capacity/timing policy regression tests."""
import argparse
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');a=p.parse_args()
    binary=ROOT/'build/online-session-policy';binary.parent.mkdir(exist_ok=True)
    cmd=['g++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-ISrc','Tests/OnlineSessionPolicyTest.cpp','Src/Util/NewConfig.cpp','Src/Util/Format.cpp','-o',str(binary)]
    if a.sanitize:cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
    subprocess.run(cmd,cwd=ROOT,check=True);subprocess.run([str(binary)],check=True)
