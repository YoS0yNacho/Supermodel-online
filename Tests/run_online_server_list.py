#!/usr/bin/env python3
"""ROM/SDL-free saved-server validation, migration and real INI persistence tests."""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='supermodel-servers-') as tmp:
        binary = Path(tmp) / 'online-server-list'
        command = ['g++', '-std=c++17', '-Wall', '-Wextra', '-Wno-unused-parameter',
                   '-O1', '-g', '-ISrc', '-ISrc/OSD/SDL',
                   'Tests/OnlineServerListTest.cpp', 'Src/Util/NewConfig.cpp',
                   'Src/Util/ConfigBuilders.cpp', 'Src/Util/Format.cpp',
                   'Src/Pkgs/tinyxml2.cpp', 'Src/OSD/Logger.cpp', '-pthread', '-o', str(binary)]
        if args.sanitize:
            command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
        subprocess.run(command, cwd=ROOT, check=True)
        subprocess.run([str(binary), str(Path(tmp) / 'Supermodel.ini')], cwd=ROOT, check=True)


if __name__ == '__main__':
    main()
