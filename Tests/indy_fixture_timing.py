#!/usr/bin/env python3
"""Inspect timing metadata in private collision fixtures; never print guest RAM.

These blocks are lab-only, platform-specific captures, not portable save states.
Version 1 lacks the original i960 slice budget and delayed-callback indicator;
version 2 includes them and a UART snapshot. Presence alone does not validate a
scheduler replay or prove that all board state was captured.
"""
import argparse
import struct
from pathlib import Path


def blocks(data):
    result = {}
    pos = 0
    while pos < len(data):
        if len(data) - pos < 12:
            raise ValueError('truncated block header')
        length, namesize, commentsize = struct.unpack_from('<III', data, pos)
        if length < 12 or not namesize or not commentsize or length > len(data) - pos or 12 + namesize + commentsize > length:
            raise ValueError('invalid block length')
        head = pos + 12
        namebytes = data[head:head + namesize]
        commentbytes = data[head + namesize:head + namesize + commentsize]
        if namebytes[-1:] != b'\0' or commentbytes[-1:] != b'\0':
            raise ValueError('invalid block strings')
        try:
            name = namebytes[:-1].decode('utf-8')
        except UnicodeDecodeError as exc:
            raise ValueError('invalid block name') from exc
        if name in result:
            raise ValueError('duplicate block')
        result[name] = data[head + namesize + commentsize:pos + length]
        pos += length
    return result


def inspect(data):
    bs = blocks(data)
    if 'Fixture timing' not in bs or 'Fixture devices' not in bs:
        raise ValueError('missing timing or device block')
    b = bs['Fixture timing']
    if len(b) < 8:
        raise ValueError('truncated timing block')
    version, n = struct.unpack_from('<II', b)
    if version not in (1, 2) or n != 0x38:
        raise ValueError('unsupported timing version or wait-state length')
    expected = 8 + n + 16 + 16 + 4 + 32 + 20 + (14 if version == 2 else 0)
    if len(b) != expected:
        raise ValueError('truncated or extended timing block')
    o = 8 + n
    vals = struct.unpack_from('<4I', b, o); o += 16
    orig = struct.unpack_from('<4I', b, o); o += 16
    running = list(b[o:o+4]); o += 4
    if any(x > 1 for x in running):
        raise ValueError('invalid timer flag')
    starts = struct.unpack_from('<4q', b, o); o += 32
    req, ena, line, uart_frac, elapsed = struct.unpack_from('<IIiii', b, o); o += 20
    if not 0 <= line < 424 or not 0 <= uart_frac < 1600 or elapsed < 0:
        raise ValueError('invalid timing values')
    device = bs['Fixture devices']
    if len(device) < 28:
        raise ValueError('truncated device block')
    dversion, cpu_now, sharc_now, line_end = struct.unpack_from('<Iqqq', device)
    if dversion != 1:
        raise ValueError('unsupported device version')
    out = dict(version=version, line=line, uart_fraction=uart_frac, elapsed=elapsed,
               cpu_now=cpu_now, sharc_now=sharc_now, line_end=line_end,
               irq_request=req, irq_enable=ena, timer_running=running,
               timer_values=vals, timer_original=orig, timer_start=starts,
               uart_saved='Fixture UART' in bs)
    if version == 2:
        slice_start, budget, in_cpu, delayed = struct.unpack_from('<qiBB', b, o)
        if in_cpu > 1 or delayed > 1 or not in_cpu or budget <= 0 or elapsed >= budget or cpu_now != slice_start + 32 * elapsed:
            raise ValueError('invalid slice state')
        if 'Fixture UART' not in bs or not bs['Fixture UART']:
            raise ValueError('missing UART snapshot')
        out.update(slice_start=slice_start, slice_budget=budget,
                   slice_remaining=budget-elapsed, delayed_callback=bool(delayed))
    return out


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('fixture', type=Path)
    args = p.parse_args()
    try:
        info = inspect(args.fixture.read_bytes())
    except (OSError, ValueError, struct.error) as e:
        p.exit(1, f'Invalid timing fixture: {e}\n')
    for k, value in info.items():
        print(f'{k}={value}')
    if info['version'] == 1:
        print('INCOMPLETE: slice budget, callback status, and UART state not captured')
    elif info['delayed_callback']:
        print('UNSUPPORTED: pending i960 delayed callback')
    else:
        print('CAPTURED: replay validation still required')


if __name__ == '__main__':
    main()
