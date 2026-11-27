#!/usr/bin/env python3
"""Experimental state-aware two-cab Indy collision capture.

Requires the diagnostic build made with IndyCollisionTrace.patch, Xvfb,
xdotool, ImageMagick import, and the Indy ROM. Output must not exist. This is a
lab, not a production build. It never forces collision by modifying guest RAM.
"""
import asyncio
import os
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'Tests'))
from run_online_lobby import Client, online, relay
from run_indy_room_options import launch
from indy_frame_input_report import parse_script, read_run, verify_script, script_coverage

OUT = Path(os.environ['INDY_COLLISION_OUTPUT']).resolve()
BIN = Path(os.environ['INDY_TRACE_BINARY']).resolve()
DISPLAY_BASE = int(os.environ.get('INDY_DISPLAY_BASE', '91'))
DISPLAYS = (DISPLAY_BASE, DISPLAY_BASE + 1)
RACE_SECONDS = int(os.environ.get('RACE_SECONDS', '100'))
START_TIMEOUT = int(os.environ.get('INDY_START_TIMEOUT', '180'))
LATENCY_MS = int(os.environ.get('INDY_LATENCY_MS', '100'))
JITTER_MS = int(os.environ.get('INDY_JITTER_MS', '8'))


async def tap(display, key, seconds=0.3):
    env = dict(os.environ, DISPLAY=f':{display}')
    subprocess.run(['xdotool', 'keydown', key], env=env, check=True)
    try:
        await asyncio.sleep(seconds)
    finally:
        subprocess.run(['xdotool', 'keyup', key], env=env, check=True)


def hold(display, key, pressed):
    subprocess.run(['xdotool', 'keydown' if pressed else 'keyup', key],
                   env=dict(os.environ, DISPLAY=f':{display}'), check=True)


def state(path):
    """Return latest complete state sample, or None if not written yet."""
    if not path.exists():
        return None
    latest = None
    with path.open() as f:
        for line in f:
            if line.startswith('S ') and line.endswith('\n'):
                parts = line.split()
                if len(parts) == 10:
                    latest = tuple(int(n) for n in parts[1:])
    return latest


def in_race(s):
    if s is None:
        return False
    frame, mode, submode, car_phase, playing, local, players, roster, active = s
    return active == 1 and 2 <= players <= 8 and local < players and roster.bit_count() >= 2


def steering_cabinet(index, target, race_samples):
    if target == 'cabinet' and index in (0, 1):
        return index
    if target == 'actor' and index in (0, 1):
        matches = [i for i, sample in enumerate(race_samples) if sample[5] == index]
        if len(matches) == 1:
            return matches[0]
    raise ValueError('invalid or ambiguous steering target')


def verify_fixtures(cabs):
    count = 0
    for cab in cabs:
        for entry in cab.glob('fixture-*.in'):
            stem = str(entry)[:-3]
            if (not Path(stem + '.out').is_file() or not Path(stem + '.io').is_file() or
                not (cab / 'fixture-program.bin').is_file() or
                (os.environ.get('SUPERMODEL_INDY_LAB_COMM_FIXTURE') and not Path(stem + '.comm').is_file())):
                raise RuntimeError(f'incomplete fixture: {entry}')
            count += 1
    if not count:
        raise RuntimeError('no collision-boundary fixtures captured')
    return count


def capture_ready(cabs, segments):
    for cab in cabs:
        state_path = cab / 'state.log'
        if not state_path.exists():
            return False
        markers = set()
        for line in state_path.read_text(errors='replace').splitlines(keepends=True):
            fields = line.split()
            if line.endswith('\n') and len(fields) == 4 and fields[0] == 'FIXTURE_END' and fields[1].isdigit():
                markers.add(fields[1])
        completed = False
        for entry in cab.glob('fixture-*.in'):
            number = entry.stem.removeprefix('fixture-')
            stem = str(entry)[:-3]
            if (number.isdigit() and number in markers and
                Path(stem + '.out').is_file() and Path(stem + '.io').is_file() and
                (not os.environ.get('SUPERMODEL_INDY_LAB_COMM_FIXTURE') or Path(stem + '.comm').is_file()) and
                (cab / 'fixture-program.bin').is_file()):
                completed = True
                break
        if not completed:
            return False
    if segments is not None:
        try:
            run = read_run(cabs[0].parent)
            return not any(verify_script(run, segments).values()) and all(x['complete'] for x in script_coverage(run, segments))
        except (FileNotFoundError, ValueError):
            return False
    return True


async def wait_for_capture(cabs, segments, seconds):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if capture_ready(cabs, segments):
            print('FIXTURES_READY_BOTH_CABS', flush=True)
            return
        await asyncio.sleep(min(1, max(0, deadline - time.monotonic())))
    raise TimeoutError('both cabinets did not produce complete fixtures in time')


def screenshot(display, path):
    subprocess.run(['import', '-display', f':{display}', '-window', 'root', str(path)],
                   timeout=8, check=False)


async def wait_for_links(cabs, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if all((cab / 'game.log').exists() and 'link established' in
               (cab / 'game.log').read_text(errors='replace') for cab in cabs):
            return
        await asyncio.sleep(1)
    raise TimeoutError('native links did not establish')


async def wait_for_attract(cabs, timeout):
    # Link establishment precedes the ROM's Checking Network screen. Coining
    # there can register a credit while the start press is silently ignored.
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        samples = [state(c / 'state.log') for c in cabs]
        if all(s and s[1] == 3 and s[4] == 0 for s in samples):
            print('ATTRACT_CONFIRMED', samples, flush=True)
            return
        await asyncio.sleep(0.5)
    raise TimeoutError('both cabinets did not reach attract mode')


async def wait_for_race(cabs, timeout):
    deadline = time.monotonic() + timeout
    last = [None, None]
    last_print = 0
    last_retry = time.monotonic()
    shot = 0
    while time.monotonic() < deadline:
        samples = [state(c / 'state.log') for c in cabs]
        for i, sample in enumerate(samples):
            # Print changes of phase and roster, not the changing frame number.
            if sample and sample[1:] != last[i]:
                print('CAB_STATE', i, sample, flush=True)
                last[i] = sample[1:]
        if all(in_race(s) for s in samples):
            print('RACE_CONFIRMED', samples, flush=True)
            return samples
        # A coin can be lost during the attract-to-entry transition. Retry only
        # on the cabinet still in attract / entry-credit mode, not in selection.
        if time.monotonic() - last_retry >= 5:
            for i, sample in enumerate(samples):
                if sample and sample[1] in (3, 4):
                    print('RETRY_COIN_START', i, sample, flush=True)
                    await tap(DISPLAYS[i], '3', 0.4)
                    await asyncio.sleep(0.4)
                    await tap(DISPLAYS[i], '3', 0.4)
                    await asyncio.sleep(0.4)
                    await tap(DISPLAYS[i], '1', 0.4)
            last_retry = time.monotonic()
        if time.monotonic() - last_print >= 20:
            for i, display in enumerate(DISPLAYS):
                screenshot(display, OUT / f'waiting-{shot}-cab{i}.png')
            last_print = time.monotonic()
            shot += 1
        await asyncio.sleep(0.5)
    raise TimeoutError('both cabinets did not enter verified two-player race')


def udp_relay_factory(legacy):
    """Apply the same impairment to UDP as to the ordered TCP relay."""
    return lambda: relay.udp.UdpRelay(legacy.latency)


async def main():
    if not BIN.is_file():
        raise ValueError('INDY_TRACE_BINARY must point to the diagnostic executable')
    if LATENCY_MS < 0 or JITTER_MS < 0 or RACE_SECONDS <= 0 or START_TIMEOUT <= 0:
        raise ValueError('latency/jitter must be nonnegative and timeouts positive')
    scripted = os.environ.get('SUPERMODEL_INDY_LAB_INPUT_SCRIPT', '')
    segments = parse_script(scripted) if scripted else None
    if scripted and os.environ.get('INDY_STEER', ''):
        raise ValueError('choose frame input script or wall-clock steering, not both')
    if os.environ.get('INDY_CAPTURE_FIXTURE') == '1' and (
        not os.environ.get('SUPERMODEL_INDY_LAB_LIVE_HISTORY') or
        os.environ.get('SUPERMODEL_INDY_LAB_LOCAL_POSE') or
        os.environ.get('SUPERMODEL_INDY_LAB_COLLISION_READS')
    ):
        raise ValueError('fixture capture requires only the live-history prototype')
    if os.environ.get('SUPERMODEL_INDY_LAB_COMM_FIXTURE') and (
        os.environ.get('INDY_CAPTURE_FIXTURE') != '1' or not os.environ.get('SUPERMODEL_INDY_LAB_SHARC_FIXTURE')
    ):
        raise ValueError('comm fixture requires collision and SHARC fixture capture')
    early_stop = os.environ.get('INDY_CAPTURE_STOP_WHEN_READY') == '1'
    if early_stop and (os.environ.get('INDY_CAPTURE_FIXTURE') != '1' or os.environ.get('INDY_STEER', '')):
        raise ValueError('early fixture stop requires fixture capture and no wall-clock steering')
    OUT.mkdir(parents=True, exist_ok=False)
    print(f'LAB_IMPAIRMENT latency_ms={LATENCY_MS} jitter_ms={JITTER_MS}', flush=True)
    processes, clients = [], []
    legacy = relay.Room(log=lambda s: print(s, flush=True), latency_ms=LATENCY_MS, jitter_ms=JITTER_MS)
    _, legacy.udp = await asyncio.get_running_loop().create_datagram_endpoint(
        udp_relay_factory(legacy), local_addr=('127.0.0.1', 0))
    hub = online.Hub(legacy, log=lambda s: print(s, flush=True), latency_ms=LATENCY_MS, jitter_ms=JITTER_MS)
    server = await asyncio.start_server(hub.handle, '127.0.0.1', 0)
    hub.start()
    port = server.sockets[0].getsockname()[1]
    cabs = [OUT / f'cab{i}' for i in range(2)]
    try:
        for _ in range(2):
            clients.append(await Client().connect(port, hello=online.INDY500_HELLO, enabled=511))
        room = await clients[0].create(online.INDY500_GAME)
        await clients[1].join(room)
        clients[0].send(b'OLT1', struct.pack('<I', room))
        raw = [await launch(c, b'OLG1') for c in clients]
        for i, packet in enumerate(raw):
            ticket = online.LAUNCH.unpack_from(packet)[4].hex()
            display, cab = DISPLAYS[i], cabs[i]
            cab.mkdir()
            (cab / 'home').mkdir()
            xvfb = subprocess.Popen(['Xvfb', f':{display}', '-screen', '0', '640x480x24',
                                     '+extension', 'GLX', '-nolisten', 'tcp'],
                                    stdout=open(cab / 'xvfb.log', 'w'), stderr=subprocess.STDOUT)
            processes.append(xvfb)
            await asyncio.sleep(1)
            if xvfb.poll() is not None:
                raise RuntimeError(f'Xvfb :{display} failed; refusing to use an existing display')
            env = dict(os.environ, DISPLAY=f':{display}', HOME=str(cab / 'home'),
                       LIBGL_ALWAYS_SOFTWARE='1', SDL_AUDIODRIVER='dummy',
                       SUPERMODEL_I960_PCHIT=str(cab / 'pc.txt'),
                       SUPERMODEL_INDY_LAB_STATE=str(cab / 'state.log'))
            if os.environ.get('INDY_CAPTURE_FIXTURE') == '1':
                env['SUPERMODEL_INDY_LAB_FIXTURE'] = str(cab / 'fixture-')
            cmd = [str(BIN), str(ROOT / 'roms/indy500.zip'), '-window', '-res=496,384',
                   '-no-sound', '-no-dsb', f'-relay=127.0.0.1:{port}',
                   f'-lobby-ticket={ticket}', f'-log-output={cab}/game.log', '-log-level=info']
            if os.environ.get('INDY_EXPERIMENT_LOCAL_COLLISION') == '1':
                cmd.append('-indy-local-collision')
            if os.environ.get('INDY_DISABLE_LOCAL_COLLISION_CAB') == str(i):
                cmd.append('-no-indy-local-collision')
            processes.append(subprocess.Popen(cmd, cwd=cab, env=env,
                                             stdout=open(cab / 'stdout.log', 'w'),
                                             stderr=subprocess.STDOUT))
        print('LAB_STARTED', flush=True)
        await wait_for_links(cabs, 120)
        print('LINKS_ESTABLISHED', flush=True)
        await wait_for_attract(cabs, 120)
        for i, display in enumerate(DISPLAYS):
            screenshot(display, OUT / f'before-coin-cab{i}.png')
        for _ in range(2):
            for display in DISPLAYS:
                await tap(display, '3')
            await asyncio.sleep(2)
        for display in DISPLAYS:
            await tap(display, '1')
        print('COIN_START_SENT', flush=True)
        race_samples = await wait_for_race(cabs, START_TIMEOUT)
        steer = os.environ.get('INDY_STEER', '')
        if scripted:
            print('FRAME_SCRIPT_ACTIVE', scripted, flush=True)
        else:
            for display in DISPLAYS:
                hold(display, 'Up', True)
            print('GAS_HELD', flush=True)
        # Optional one-shot steering pulse, for exploratory contact attempts.
        # Syntax: cabinet index,Left|Right,delay seconds,duration seconds.
        if early_stop:
            await wait_for_capture(cabs, segments, RACE_SECONDS)
        elif steer:
            index, key, delay, duration = steer.split(',')
            index, delay, duration = int(index), float(delay), float(duration)
            if key not in ('Left', 'Right') or delay < 0 or duration <= 0 or delay + duration > RACE_SECONDS:
                raise ValueError('invalid INDY_STEER')
            cab = steering_cabinet(index, os.environ.get('INDY_STEER_TARGET', 'cabinet'), race_samples)
            await asyncio.sleep(delay)
            print('STEER', cab, key, duration, 'local_actor', race_samples[cab][5], flush=True)
            await tap(DISPLAYS[cab], key, duration)
            await asyncio.sleep(RACE_SECONDS-delay-duration)
        else:
            await asyncio.sleep(RACE_SECONDS)
        if not scripted:
            for display in DISPLAYS:
                hold(display, 'Up', False)
        for i, display in enumerate(DISPLAYS):
            screenshot(display, OUT / f'final-cab{i}.png')
        if scripted:
            run = read_run(OUT)
            mismatches = verify_script(run, segments)
            coverage = script_coverage(run, segments)
            if any(mismatches.values()) or not all(item['complete'] for item in coverage):
                raise RuntimeError(f'frame script mismatches: {mismatches}, coverage: {coverage}')
            print('FRAME_SCRIPT_VERIFIED', mismatches, 'coverage', coverage, flush=True)
        if os.environ.get('INDY_CAPTURE_FIXTURE') == '1':
            print('FIXTURES_COMPLETE', verify_fixtures(cabs), flush=True)
        print('LAB_CAPTURE_COMPLETE', flush=True)
    finally:
        for p in processes:
            if p.poll() is None:
                p.terminate()
        for p in processes:
            try:
                p.wait(timeout=8)
            except subprocess.TimeoutExpired:
                p.kill()
                p.wait()
        server.close()
        await server.wait_closed()
        await hub.close()
        await legacy.close()
        for c in clients:
            c.close()


if __name__ == '__main__':
    asyncio.run(main())
