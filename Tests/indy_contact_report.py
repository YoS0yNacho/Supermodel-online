#!/usr/bin/env python3
"""Summarize Indy diagnostic S/H/I records; frame IDs are cabinet-local.

H: collision-candidate flag written by ROM 0xac338, with the selected actor
and cached local, local physics and selected actor X/Z. The selected flag does
not guarantee that an impulse was applied. I: local-physics position write at
ROM 0x20ec4 or 0x20ed0. Same-frame association is suggestive, not proof of
causality; do not compare frame IDs between cabinets as synchronized time.
"""
import argparse
from collections import Counter, defaultdict
import json
import math
from pathlib import Path
import statistics
import struct


def summarize(path, physics_hook_present=False):
    contacts = []
    impulse_frames = defaultdict(set)
    read_substitutions = defaultdict(dict)
    race_samples = 0
    local_indices = set()
    race_rosters = set()
    truncated = False
    with Path(path).open(encoding='ascii') as f:
        for line_number, line in enumerate(f, 1):
            if not line.endswith('\n'):
                truncated = True
                break
            s = line.split()
            if not s:
                continue
            try:
                if s[0] == 'S':
                    if len(s) != 10:
                        raise ValueError('expected 10 S fields')
                    frame, mode, submode, phase, playing, local, players, roster, active = map(int, s[1:])
                    if active and players >= 2 and local < players and roster.bit_count() >= 2:
                        race_samples += 1
                        local_indices.add(local)
                        race_rosters.add(players)
                elif s[0] == 'H':
                    if len(s) != 11:
                        raise ValueError('expected 11 H fields')
                    frame, local, candidate = map(int, s[1:4])
                    pointer = int(s[4], 16)
                    coords = tuple(map(float, s[5:]))
                    if not all(map(math.isfinite, coords)):
                        raise ValueError('non-finite H coordinates')
                    lx, lz, px, pz, ox, oz = coords
                    contacts.append(dict(frame=frame, local=local, candidate=candidate,
                                         pointer=f'{pointer:08x}',
                                         cache_to_physics=math.hypot(lx-px, lz-pz),
                                         cache_to_candidate=math.hypot(lx-ox, lz-oz),
                                         physics_to_candidate=math.hypot(px-ox, pz-oz)))
                elif s[0] == 'I':
                    if len(s) != 7:
                        raise ValueError('expected 7 I fields')
                    frame, pc = int(s[1]), int(s[2], 16)
                    if pc in (0x20ec4, 0x20ed0):
                        impulse_frames[frame].add(pc)
                elif s[0] == 'Q':
                    if len(s) != 6:
                        raise ValueError('expected 6 Q fields')
                    frame, pc, local = int(s[1]), int(s[2], 16), int(s[3])
                    raw, value = (struct.unpack('<f', int(v, 16).to_bytes(4, 'little'))[0]
                                  for v in s[4:6])
                    if not math.isfinite(raw) or not math.isfinite(value):
                        raise ValueError('non-finite Q value')
                    # Last observation per PC/frame; not a synchronization ID.
                    read_substitutions[frame, local][pc] = (raw, value)
            except (ValueError, IndexError) as exc:
                raise ValueError(f'{path}:{line_number}: {exc}') from exc
    for event in contacts:
        event['same_frame_physics_xy_writes'] = (len(impulse_frames[event['frame']]) == 2
                                                  if physics_hook_present else None)
    # The two-human classification is explicit; other modes/rosters need more
    # metadata. Do not classify candidate 0 or 1 as human in an unverified log.
    two_player = bool(race_samples and race_rosters == {2} and len(local_indices) == 1 and local_indices <= {0, 1})
    peer = [e for e in contacts if two_player and e['local'] in local_indices
            and e['candidate'] == 1 - e['local']]
    def stats(key):
        values = [e[key] for e in peer]
        return {'mean': statistics.fmean(values), 'max': max(values)} if values else None
    # Check that the experiment's history shift preserves the native delta,
    # except where its explicit >=8-unit reset/warp clamp zeroes that delta.
    pairs = ((0xac4e8, 0xac504), (0xac4fc, 0xac510),
             (0xac2ac, 0xac2c4), (0xac2bc, 0xac2cc))
    checked = clamped = mismatch = 0
    for reads in read_substitutions.values():
        for current, history in pairs:
            if current not in reads or history not in reads:
                continue
            old_current, new_current = reads[current]
            old_previous, new_previous = reads[history]
            old_delta = old_current - old_previous
            new_delta = new_current - new_previous
            checked += 1
            expected = old_delta if abs(old_delta) < 8 else 0.0
            clamped += abs(old_delta) >= 8
            mismatch += not math.isclose(new_delta, expected, abs_tol=0.001)
    return {'path': str(path), 'truncated_tail': truncated,
            'valid_two_player_race_samples': race_samples, 'local_indices': sorted(local_indices),
            'candidate_flags': len(contacts), 'candidates': dict(Counter(str(e['candidate']) for e in contacts)),
            'peer_candidate_flags': len(peer), 'physics_hook_present': physics_hook_present,
            'peer_flags_with_same_frame_physics_xy_writes': (sum(e['same_frame_physics_xy_writes'] for e in peer)
                                                            if physics_hook_present else None),
            'peer_cache_to_physics': stats('cache_to_physics'),
            'peer_physics_to_candidate': stats('physics_to_candidate'),
            'physics_xy_write_frames': (sum(len(pcs) == 2 for pcs in impulse_frames.values())
                                        if physics_hook_present else None),
            'experimental_read_history_pairs': checked,
            'experimental_history_clamps': clamped,
            'experimental_history_mismatches': mismatch,
            'peer_events': peer}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('logs', nargs='+')
    parser.add_argument('--physics-hook-present', action='store_true',
                        help='confirm every supplied log was captured with I instrumentation')
    args = parser.parse_args()
    print(json.dumps([summarize(p, args.physics_hook_present) for p in args.logs], indent=2, allow_nan=False))


if __name__ == '__main__':
    main()
