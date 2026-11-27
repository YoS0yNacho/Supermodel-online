#!/usr/bin/env python3
"""Check K records from the opt-in experimental local collision input.

This verifies instrumentation consistency, NOT correct physics or gameplay.
"""
import argparse
from collections import Counter
import math
from pathlib import Path
import struct

SITES = {0xac4e8: 'x', 0xac2ac: 'x', 0xac4fc: 'z', 0xac2bc: 'z',
         0xac504: 'px', 0xac2c4: 'px', 0xac510: 'pz', 0xac2cc: 'pz'}


def analyze(path):
    frames = {}
    counts = Counter()
    for number, line in enumerate(Path(path).read_text(errors='strict').splitlines(keepends=True), 1):
        if not line.endswith('\n'):
            break  # capture may end while a line is being written
        if not line.startswith('K '):
            continue
        f = line.split()
        if len(f) != 6:
            raise ValueError(f'malformed K record at line {number}')
        frame, pc, actor = int(f[1]), int(f[2], 16), int(f[3])
        native, replacement = int(f[4], 16), int(f[5], 16)
        if pc not in SITES or actor not in (0, 1) or not (0 <= native <= 0xffffffff and 0 <= replacement <= 0xffffffff):
            raise ValueError(f'invalid K record at line {number}')
        value = struct.unpack('<f', struct.pack('<I', replacement))[0]
        if not math.isfinite(value) or abs(value) >= 8192:
            raise ValueError(f'invalid replacement at line {number}')
        entry = frames.setdefault((actor, frame), {})
        field = SITES[pc]
        if field in entry and entry[field] != replacement:
            raise ValueError(f'inconsistent same-frame {field} at line {number}')
        entry[field] = replacement
        counts['records'] += 1
        counts['changed'] += native != replacement
        counts[f'pc_{pc:06x}'] += 1
    for (actor, frame), values in frames.items():
        old = frames.get((actor, frame-1))
        if old:
            for curr, prev in (('x', 'px'), ('z', 'pz')):
                if curr in old and prev in values:
                    if old[curr] != values[prev]:
                        raise ValueError(f'history mismatch for actor {actor} frame {frame} {prev}')
                    counts['history_checks'] += 1
    counts['frames'] = len(frames)
    return counts


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('state_log', nargs='+')
    for path in p.parse_args().state_log:
        result = analyze(path)
        print(path, ' '.join(f'{k}={v}' for k, v in sorted(result.items())), 'physics=unvalidated')
        if not result['records']:
            raise SystemExit('no experimental records; option may not have been active')

if __name__ == '__main__':
    main()
