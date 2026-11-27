#!/usr/bin/env python3
"""Summarize an Indy i960 write-watch trace, without treating proximity as proof of contact.

Input records: P <frame> <actor> <x> <z> and
W <frame> <pc-hex> <address-hex> <data-hex> <mask-hex>.
P records are sampled at vblank; W records occur later in the same frame. The
actual contact partner and instantaneous distance can therefore differ from
nearest-at-vblank. Logs must include all relevant actor P records to make nearest
comparisons meaningful. Frames are cabinet-local, not synchronized clocks.
"""
import argparse
import json
import math
from pathlib import Path

BASE = 0x53c7a0
STRIDE = 0x1e4
ACTORS = 13
TRIGGER_PC = 0xaa71c


def analyze(path, first_frame=0, local_actor=None):
    positions, triggered = {}, []
    current = None
    frames = valid_frames = 0
    truncated_tail = False
    minimum = None
    events = []
    counts = []

    def finish():
        nonlocal frames, valid_frames, minimum
        if current is None or current < first_frame:
            return
        frames += 1
        counts.append(len(positions))
        usable = {a: p for a, p in positions.items()
                  if all(math.isfinite(v) and abs(v) < 8192 for v in p)}
        # Zero-filled startup RAM is not a meaningful car position. All-zero
        # snapshots are excluded, but an individual position at the origin is valid.
        if usable and any(x != 0 or z != 0 for x, z in usable.values()):
            valid_frames += 1
            if local_actor is not None and local_actor in usable:
                for actor, pos in usable.items():
                    if actor == local_actor:
                        continue
                    dist = math.dist(usable[local_actor], pos)
                    if minimum is None or dist < minimum['distance']:
                        minimum = dict(frame=current, actor=actor, distance=dist)
            for actor in triggered:
                others = [(math.dist(usable[actor], pos), a)
                          for a, pos in usable.items() if a != actor] if actor in usable else []
                near = min(others) if others else None
                events.append(dict(frame=current, actor=actor,
                                   nearest_actor=near[1] if near else None,
                                   distance=near[0] if near else None,
                                   tracked_actors=len(positions)))
        else:
            for actor in triggered:
                events.append(dict(frame=current, actor=actor, nearest_actor=None,
                                   distance=None, tracked_actors=len(positions)))

    with Path(path).open(encoding='ascii') as source:
        for line_no, line in enumerate(source, 1):
            # A terminated emulator can leave a partially written last record.
            # Discard it explicitly; malformed complete records still fail.
            if not line.endswith('\n'):
                truncated_tail = True
                break
            fields = line.split()
            if not fields or fields[0] not in ('P', 'W'):
                continue
            try:
                frame = int(fields[1])
                if current is not None and frame < current:
                    raise ValueError('frame numbers decrease')
                if frame != current:
                    finish()
                    current, positions, triggered = frame, {}, []
                if fields[0] == 'P':
                    if len(fields) != 5:
                        raise ValueError('expected five P fields')
                    actor = int(fields[2])
                    if 0 <= actor < ACTORS:
                        positions[actor] = (float(fields[3]), float(fields[4]))
                else:
                    if len(fields) != 6:
                        raise ValueError('expected six W fields')
                    pc, address, value, mask = (int(x, 16) for x in fields[2:])
                    offset = address - BASE
                    if (pc == TRIGGER_PC and offset >= 0 and
                            offset // STRIDE < ACTORS and offset % STRIDE == 0x104 and
                            value == 30 and mask == 0xffffffff):
                        triggered.append(offset // STRIDE)
            except (ValueError, IndexError) as exc:
                raise ValueError(f'{path}:{line_no}: {exc}') from exc
    finish()
    return dict(path=str(path), first_frame=first_frame, observed_frames=frames,
                truncated_tail=truncated_tail, valid_position_frames=valid_frames,
                min_actors_tracked=min(counts) if counts else 0,
                max_actors_tracked=max(counts) if counts else 0,
                local_actor=local_actor, nearest_local_sample=minimum,
                trigger_count=len(events), triggers=events)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('logs', nargs='+', help='trace files, one per cabinet')
    p.add_argument('--first-frame', type=int, default=0,
                   help='exclude earlier frames (cabinet-local, not shared time)')
    p.add_argument('--local-actor', type=int, choices=range(ACTORS),
                   help='actor index to report nearest tracked car; confirm separately per cab')
    args = p.parse_args()
    print(json.dumps([analyze(log, args.first_frame, args.local_actor) for log in args.logs],
                     indent=2, allow_nan=False))


if __name__ == '__main__':
    main()
