#!/usr/bin/env python3
"""Gated, explicitly unvalidated peer-response exploration on private fixtures.

This verifies the *captured experimental subset* before running the candidate
native detector and (only if it selects the same peer) native response. It
cannot establish whole-board equivalence or gameplay correctness. Never publish
fixture/ROM files. A matching subset is not a validated counterfactual.
"""
import argparse
import re
import subprocess
import sys

BOUNDARY = re.compile(r'^BOUNDARY cpu=1 sharc=1 uart=1 memory=1 fifo_buffer=1 events_including_stalls=1 timing=1 flags=1(?: |$)', re.M)
PATH = 'UNVALIDATED_REPLAY_PATH selected_peer=1 flag=1 detector_return=1 physics_read=1 physics_call=1 x_store=1 z_store=1'
COMM = re.compile(r'^COMM_DIAGNOSTIC captured=([01]) unchanged=([01]) events_equal=([01]) reads=([0-9]+) ', re.M)


def run(binary, files, mode=None):
    command = [binary, *files]
    if mode:
        command.append(mode)
    return subprocess.run(command, capture_output=True, text=True, timeout=60)


def explore(binary, files):
    baseline = run(binary, files)
    print('=== experimental boundary check ===')
    print(baseline.stdout, end='')
    if baseline.stderr:
        print(baseline.stderr, end='', file=sys.stderr)
    comm = COMM.search(baseline.stdout)
    comm_ok = bool(comm and (comm.group(1, 2, 3) == ('0', '0', '0') or
                             comm.group(1, 2, 3) == ('1', '1', '1')))
    if baseline.returncode != 1 or baseline.stderr or not BOUNDARY.search(baseline.stdout) or PATH not in baseline.stdout or not comm_ok:
        print('STOP: experimental peer-response subset did not match; native not run.')
        return 1
    if comm.group(1) == '0':
        print('NOTE: this fixture has no comm snapshot; comm state is unvalidated.')
    elif not re.search(r'^COMM_WITNESS version=2 entrypoints=1 ', baseline.stdout, re.M):
        print('NOTE: older comm snapshot has no mutation-entry witness; transient changes are unvalidated.')
    print('NOTE: matched subset only; full board and external inputs are unvalidated.')
    detector = run(binary, files, '--explore-detector-native')
    print('=== exploratory native detector ===')
    print(detector.stdout, end='')
    if detector.stderr:
        print(detector.stderr, end='', file=sys.stderr)
    marker = re.search(r'^EXPLORATORY_UNVALIDATED mode=native peer_selected=1 flag=1 ', detector.stdout, re.M)
    if detector.returncode != 1 or detector.stderr or not marker:
        print('STOP: native same-peer selection not demonstrated; no comparable native response run.')
        return 1
    response = run(binary, files, '--explore-response-native')
    print('=== exploratory native response ===')
    print(response.stdout, end='')
    if response.stderr:
        print(response.stderr, end='', file=sys.stderr)
    native_comm = COMM.search(response.stdout)
    same_comm_check = bool(native_comm and native_comm.group(1, 2, 3) == comm.group(1, 2, 3))
    if response.returncode != 1 or response.stderr or not re.search(r'^EXPLORATORY_UNVALIDATED_RESPONSE native_same_peer=1 ', response.stdout, re.M) or not same_comm_check:
        print('STOP: native response was unsupported or did not follow the same path.')
        return 1
    print('INCONCLUSIVE: no full-board validation, causal gameplay result, or production recommendation.')
    return 1


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('binary')
    p.add_argument('fixture_in')
    p.add_argument('fixture_out')
    p.add_argument('program')
    p.add_argument('main_data')
    p.add_argument('copro_data')
    p.add_argument('io')
    a = p.parse_args()
    try:
        return explore(a.binary, [a.fixture_in,a.fixture_out,a.program,a.main_data,a.copro_data,a.io])
    except (OSError, subprocess.TimeoutExpired) as e:
        print(f'INCONCLUSIVE: replay could not be completed: {e}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
