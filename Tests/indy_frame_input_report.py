#!/usr/bin/env python3
"""Compare B-recorded frame scripts and paths, not collision correctness.

Relative frames start independently on each cabinet, at first eligible active
frame. Clocks are comparable only for two processes on the same host. Matched
input is not deterministic replay; packet delivery and initial states differ.
"""
import argparse
from collections import Counter
import json
import math
from pathlib import Path
import statistics


def parse_script(text):
    segments=[]
    for chunk in text.split(';'):
        fields=chunk.split(',')
        if len(fields)!=6 or any(not x or len(x)>10 or not x.isascii() or not x.isdigit() for x in fields):
            raise ValueError('invalid frame input script')
        actor, begin, end, steer, accel, brake=map(int,fields)
        if actor not in (0,1) or begin>=end or end>100000 or any(x>255 for x in (steer,accel,brake)):
            raise ValueError('invalid frame input script')
        if any(a==actor and begin<e and b<end for a,b,e,_ in segments):
            raise ValueError('overlapping frame input script')
        segments.append((actor,begin,end,(steer,accel,brake)))
    return segments


def expected_input(segments, actor, frame):
    for a,begin,end,command in segments:
        if a==actor and begin<=frame<end:
            return command
    return (128,255,0)


def verify_script(run, segments):
    return {str(actor): sum(r[4]!=expected_input(segments,actor,r[2]) for r in data['rows'])
            for actor,data in run.items()}


def script_coverage(run, segments):
    result=[]
    for actor,begin,end,command in segments:
        observed=sum(begin<=r[2]<end for r in run[actor]['rows'])
        result.append(dict(actor=actor, begin=begin, end=end, observed_frames=observed,
                           expected_frames=end-begin, complete=observed==end-begin))
    return result


def read_run(directory):
    result = {}
    for cabinet in (0, 1):
        path = Path(directory) / f'cab{cabinet}' / 'state.log'
        rows = []
        truncated = invalid = False
        with path.open(encoding='ascii') as f:
            for number, line in enumerate(f, 1):
                if not line.endswith('\n'):
                    truncated = True
                    break
                s = line.split()
                if not s:
                    continue
                if s[0] == 'BAD_INPUT_SCRIPT':
                    invalid = True
                if s[0] != 'B':
                    continue
                try:
                    if len(s) != 12:
                        raise ValueError('expected 12 B fields')
                    frame, us, relative, actor, steer, accel, brake = map(int, s[1:8])
                    local = tuple(map(float, s[8:10]))
                    peer = tuple(map(float, s[10:12]))
                    if actor not in (0,1) or min(frame,us,relative)<0 or any(not 0<=v<=255 for v in (steer,accel,brake)) or not all(map(math.isfinite,local+peer)):
                        raise ValueError('invalid B field')
                    rows.append((frame,us,relative,actor,(steer,accel,brake),local,peer))
                except ValueError as exc:
                    raise ValueError(f'{path}:{number}: {exc}') from exc
        if invalid or not rows:
            raise ValueError(f'{path}: input script invalid or no B records')
        actors = {r[3] for r in rows}
        if len(actors)!=1:
            raise ValueError(f'{path}: actor changed during capture; cannot compare')
        actor=next(iter(actors))
        if actor in result:
            raise ValueError(f'{directory}: both cabinets report the same actor')
        relative = [r[2] for r in rows]
        discontinuities = sum(b!=a+1 for a,b in zip(relative,relative[1:]))
        if relative[0]!=0 or discontinuities:
            raise ValueError(f'{path}: relative-frame sequence is incomplete or restarted')
        if any(b[0]!=a[0]+1 or b[1]<a[1] for a,b in zip(rows,rows[1:])):
            raise ValueError(f'{path}: emulator frame or local clock sequence is invalid')
        result[actor] = dict(cabinet=cabinet, rows=rows, truncated=truncated)
    if set(result)!={0,1}:
        raise ValueError(f'{directory}: both actors required')
    return result


def overview(run):
    result={}
    for actor, data in run.items():
        rows=data['rows']
        best=min((math.dist(r[5],r[6]),r[2]) for r in rows)
        counts=Counter(','.join(map(str,r[4])) for r in rows)
        result[str(actor)] = dict(cabinet=data['cabinet'], count=len(rows),
                                  last_relative_frame=rows[-1][2],
                                  input_counts=dict(counts), truncated_tail=data['truncated'],
                                  min_local_to_cached_peer_distance=best[0],
                                  min_distance_relative_frame=best[1])
    result['actor_start_clock_skew_ms']=(run[1]['rows'][0][1]-run[0]['rows'][0][1])/1000
    return result


def compare(a,b):
    result={}
    for actor in (0,1):
        left=a[actor]['rows']; right=b[actor]['rows']
        count=min(len(left),len(right))
        diffs=[math.dist(x[5],y[5]) for x,y in zip(left[:count],right[:count])]
        mismatches=sum(x[4]!=y[4] for x,y in zip(left[:count],right[:count]))
        checkpoints={str(n):diffs[n] for n in (0,30,60,120,300,600,900,1200) if n<count}
        result[str(actor)] = dict(compared_frames=count, input_mismatches=mismatches,
                                  local_position_difference_mean=statistics.fmean(diffs),
                                  local_position_difference_max=max(diffs),
                                  local_position_difference_at_frame=checkpoints)
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('baseline'); p.add_argument('experiment')
    p.add_argument('--script', help='verify each B record against the intended script')
    args=p.parse_args()
    a,b=read_run(args.baseline),read_run(args.experiment)
    report=dict(baseline=overview(a), experiment=overview(b), comparison=compare(a,b))
    if args.script is not None:
        segments=parse_script(args.script)
        report['script_mismatches']={'baseline':verify_script(a,segments),
                                     'experiment':verify_script(b,segments)}
        report['script_coverage']={'baseline':script_coverage(a,segments),
                                   'experiment':script_coverage(b,segments)}
    print(json.dumps(report, indent=2, allow_nan=False))

if __name__=='__main__': main()
