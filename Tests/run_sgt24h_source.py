#!/usr/bin/env python3
"""Source-only SGT suite. No emulator, NVRAM, deployed relay or GUI lab.

Compiles isolated helpers into a temporary directory, optionally verifies a
user-supplied ROM, and runs in-process loopback lobby/transport fixtures.
This does NOT certify linked races, local response, UDP or visual quality.
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom', type=Path, help='optional sgt24h.zip for actual-ROM assertions')
    parser.add_argument('--sanitize', action='store_true', help='ASan/UBSan for C++ helpers')
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
    parser.add_argument('--integration', action='store_true', help='compile/run real C++ client and session policy; requires SDL development libraries')
    parser.add_argument('--pre-sgt24h-relay', type=Path, help='optional older relay source for C++ greeting fallback; requires --integration')
    args = parser.parse_args()
    rom = args.rom.resolve() if args.rom else None
    if rom is not None and not rom.is_file():
        parser.error(f'ROM not found: {rom}')
    if args.pre_sgt24h_relay and (not args.integration or not args.pre_sgt24h_relay.is_file()):
        parser.error('--pre-sgt24h-relay requires --integration and an existing relay.py')
    def run(command):
        subprocess.run(command, cwd=ROOT, check=True, timeout=300)
    with tempfile.TemporaryDirectory(prefix='sgt-source-tests-') as temp:
        for name in ('Profile', 'RelayEeprom', 'Position', 'Orientation', 'Inputs', 'FullSlots', 'Selection', 'Membership', 'RingMembership', 'CopyContext', 'Freshness', 'Prediction'):
            target = str(Path(temp) / name)
            command = [args.cxx, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-ISrc']
            if args.sanitize:
                command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
            run(command + [f'Tests/Sgt24h{name}Test.cpp', '-o', target])
            run([target])
            print(f'PASS SGT {name}', flush=True)
    if rom:
        run([sys.executable, 'Tests/run_sgt24h_rom_copy.py', str(rom)])
    else:
        print('SKIP actual-ROM checks: supply --rom /path/to/sgt24h.zip', flush=True)
    for script in ('run_sgt24h_online.py', 'run_sgt24h_transport.py', 'run_sgt24h_pose_udp.py'):
        run([sys.executable, f'Tests/{script}'])
    if args.integration:
        flags = ['--sanitize'] if args.sanitize else []
        run([sys.executable, 'Tests/run_online_session_policy.py'] + flags)
        client = [sys.executable, 'Tests/run_online_client.py'] + flags
        if args.pre_sgt24h_relay:
            client += ['--pre-sgt24h-relay', str(args.pre_sgt24h_relay.resolve())]
        run(client)
    else:
        print('SKIP C++ client/policy integration: supply --integration', flush=True)
    print('PASS source-only suite; linked race and local-control behavior remain unverified.', flush=True)

if __name__ == '__main__':
    main()
