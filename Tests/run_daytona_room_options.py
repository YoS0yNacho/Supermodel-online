#!/usr/bin/env python3
"""Game-specific Daytona controls, synthetic ROM patch guards and relay parity."""
import argparse
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import relay

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    binary = ROOT / 'build/daytona-room-options-test'
    binary.parent.mkdir(exist_ok=True)
    cmd = ['g++', '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-ISrc',
           'Tests/DaytonaRoomOptionsTest.cpp', '-o', str(binary)]
    if args.sanitize:
        cmd += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
    subprocess.run(cmd, cwd=ROOT, check=True)
    subprocess.run([str(binary)], cwd=ROOT, check=True)
    rows = subprocess.check_output([str(binary), '--matrix'], text=True).splitlines()
    assert len(rows) == 9
    for game, row in enumerate(rows):
        assert len(row) == 1 << 14
        for options, value in enumerate(row):
            assert (value == '1') == relay.lobby.valid_room_options(game, options), (game, options)
    print('PASS Model 3 client/relay option validity parity: 147456 combinations')
