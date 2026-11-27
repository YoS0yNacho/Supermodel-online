#!/usr/bin/env python3
"""Check experimental Indy live-history L records; no gameplay quality claim."""
import argparse
from collections import Counter, defaultdict
import json
import math
from pathlib import Path
import struct

# Candidate and response current/history X/Z read sites, respectively.
PAIRS = ((0xac4e8, 0xac504), (0xac4fc, 0xac510),
         (0xac2ac, 0xac2c4), (0xac2bc, 0xac2cc))


def summarize(path):
    frames = defaultdict(dict)
    counts = Counter()
    truncated = False
    with Path(path).open(encoding='ascii') as stream:
        for line_number, line in enumerate(stream, 1):
            if not line.endswith('\n'):
                truncated = True
                break
            s = line.split()
            if not s or s[0] != 'L':
                continue
            try:
                if len(s) != 7:
                    raise ValueError('expected seven L fields')
                frame, pc, actor, reset = int(s[1]), int(s[2], 16), int(s[3]), int(s[6])
                if reset not in (0, 1):
                    raise ValueError('reset must be 0 or 1')
                value = struct.unpack('<f', int(s[5], 16).to_bytes(4, 'little'))[0]
                if not math.isfinite(value):
                    raise ValueError('non-finite substituted coordinate')
                counts[f'{pc:06x}'] += 1
                # The last observation at a PC in a frame is used; a frame is
                # only an emulator-local ID and is not a cross-cabinet clock.
                frames[frame, actor][pc] = (value, reset)
            except (ValueError, IndexError, OverflowError) as exc:
                raise ValueError(f'{path}:{line_number}: {exc}') from exc
    checks = mismatches = reset_pairs = response_checks = response_mismatches = 0
    for (frame, actor), values in frames.items():
        for current, history in PAIRS[:2]:
            old = frames.get((frame-1, actor), {})
            if current not in values or history not in values or current not in old:
                continue
            now, reset = values[current]
            previous, prev_reset = values[history]
            if reset != prev_reset:
                mismatches += 1
            expected = now if reset else old[current][0]
            checks += 1
            reset_pairs += reset
            mismatches += not math.isclose(previous, expected, abs_tol=0.001)
        for (a, b) in ((0xac4e8, 0xac2ac), (0xac4fc, 0xac2bc),
                       (0xac504, 0xac2c4), (0xac510, 0xac2cc)):
            if a in values and b in values:
                response_checks += 1
                response_mismatches += not math.isclose(values[a][0], values[b][0], abs_tol=0.001)
    return dict(path=str(path), truncated_tail=truncated, read_counts=dict(counts),
                sequential_history_checks=checks, reset_history_checks=reset_pairs,
                history_mismatches=mismatches, response_latch_checks=response_checks,
                response_latch_mismatches=response_mismatches)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('logs', nargs='+')
    args = p.parse_args()
    print(json.dumps([summarize(path) for path in args.logs], indent=2))


if __name__ == '__main__':
    main()
