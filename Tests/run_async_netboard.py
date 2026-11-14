#!/usr/bin/env python3
"""Build and run ROM-free async HLE tests through real delayed TCP links.
Requires g++, SDL2/SDL2_net development packages and Python 3.8+ (Linux/macOS).
Run from anywhere: python3 Tests/run_async_netboard.py [--sanitize]
"""
import argparse
import asyncio
import os
from pathlib import Path
import shlex
import random
import socket
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
# Do not reuse a previous test's listener while its connections are in TIME_WAIT.
# Keep listeners outside common Linux/macOS ephemeral-client port ranges.
TEST_PORTS = list(range(20000, 27000))
random.SystemRandom().shuffle(TEST_PORTS)
TEST_PORTS = iter(TEST_PORTS)


def build(sanitize, research=False):
    output = ROOT / "build" / "async-netboard-test"
    output.parent.mkdir(exist_ok=True)
    sources = ["Tests/AsyncNetboardTest.cpp", "Src/Model3/SimNetBoard.cpp",
               "Src/Network/Games/Daytona2/NetBoardHooks.cpp",
               "Src/Network/Games/GameNetBoard.cpp",
               "Src/Network/Games/Scud/ScudNetBoard.cpp",
               "Src/Network/Games/Srally2/Srally2NetBoard.cpp",
               "Src/Network/Games/Lemans24/Lemans24NetBoard.cpp",
               "Src/Model3/NetBoardLink.cpp", "Src/Network/NetServer.cpp",
               "Src/Network/TCPConnection.cpp", "Src/Util/NewConfig.cpp",
               "Src/Util/Format.cpp", "Src/OSD/Thread.cpp"]
    sdl = shlex.split(subprocess.check_output(["sdl2-config", "--cflags", "--libs"], text=True))
    cmd = ["g++", "-std=c++17", "-g", "-O1", "-pthread", "-ISrc", "-ISrc/OSD",
           "-ISrc/OSD/SDL", "-ISrc/Util"]
    if research:
        cmd += ["-DSUPERMODEL_SRALLY2_EIGHT_RESEARCH"]
    if sanitize:
        cmd += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie"]
    subprocess.run(cmd + sources + sdl + ["-lSDL2_net", "-o", str(output)], cwd=ROOT, check=True)
    return output


async def forward(reader, writer, latency, impaired=False):
    # Delay each chunk by a fixed amount without imposing an extra per-chunk
    # bandwidth cap. Fragmentation/coalescing remains TCP's responsibility.
    queue = asyncio.Queue()

    async def receive():
        try:
            while True:
                data = await reader.read(65536)
                if not data:
                    break
                # Ordered TCP-delivery stalls/jitter: model retransmission delay,
                # NEVER remove bytes from the reliable stream (not IP loss injection).
                extra = 0
                if impaired:
                    sequence[0] += 1
                    extra = (sequence[0] * 17 % 61) / 1000
                    if sequence[0] % 11 == 0: extra += .65
                await queue.put((time.monotonic() + latency + extra, data))
        finally:
            await queue.put((time.monotonic() + latency, None))

    sequence = [0]
    producer = asyncio.create_task(receive())
    try:
        while True:
            due, data = await queue.get()
            await asyncio.sleep(max(0, due - time.monotonic()))
            if data is None:
                break
            writer.write(data)
            await writer.drain()
    except (ConnectionError, OSError):
        pass
    finally:
        producer.cancel()
        await asyncio.gather(producer, return_exceptions=True)
        writer.close()


async def case(binary, scenario, game, n, latency):
    # Reserve unique ports until all proxy listeners have been installed.
    reservations = []
    for _ in range(2 * n):
        s = socket.socket()
        for port in TEST_PORTS:
            try:
                s.bind(("0.0.0.0", port))
                break
            except OSError:
                continue
        else:
            s.close()
            raise RuntimeError("No free test listener port in 20000..26999")
        reservations.append(s)
    ports = [s.getsockname()[1] for s in reservations]
    incoming = ports[:n]
    outgoing = ports[n:]
    tasks = set()
    servers = []

    async def proxy(reader, writer, target):
        try:
            deadline = time.monotonic() + 0.5
            while True:
                try:
                    r, w = await asyncio.open_connection("127.0.0.1", target)
                    break
                except ConnectionRefusedError:
                    if time.monotonic() >= deadline:
                        raise
                    await asyncio.sleep(0.005)
            await asyncio.gather(forward(reader, w, latency, scenario == "internet"),
                                 forward(r, writer, latency, scenario == "internet"))
        except (ConnectionError, OSError):
            writer.close()

    for i in range(n):
        reservations[n + i].close()

        def accept(reader, writer, target=incoming[(i + 1) % n]):
            task = asyncio.create_task(proxy(reader, writer, target))
            tasks.add(task)
            task.add_done_callback(tasks.discard)

        servers.append(await asyncio.start_server(accept, "127.0.0.1", outgoing[i]))
    for s in reservations[:n]:
        s.close()
    # HLE permits single-cabinet self-link only when the configured ports match.
    if n == 1:
        outgoing = incoming
    print(f"TEST {scenario} {game} n={n} per-hop delay={1000 * latency:g} ms", flush=True)
    # SDL2 >= 2.32 keeps a per-thread error buffer (realloc inside libSDL2)
    # that it never frees for threads it did not create; LeakSanitizer reports
    # it from the net server thread. Not ours: suppress leaks rooted in libSDL2.
    env = dict(os.environ)
    env["LSAN_OPTIONS"] = ":".join(filter(None, [env.get("LSAN_OPTIONS", ""),
                                                 f"suppressions={ROOT / 'Tests' / 'lsan.supp'}"]))
    proc = await asyncio.create_subprocess_exec(str(binary), scenario, game, str(n),
                                              *map(str, incoming + outgoing), cwd=ROOT, env=env,
                                              stdout=asyncio.subprocess.PIPE,
                                              stderr=asyncio.subprocess.STDOUT)
    try:
        stdout, _ = await asyncio.wait_for(proc.communicate(), 35)
        print(stdout.decode(), end="", flush=True)
        if proc.returncode:
            raise RuntimeError(f"test exited {proc.returncode}")
    except BaseException:
        if proc.returncode is None:
            proc.kill()
            await proc.wait()
        raise
    finally:
        for server in servers:
            server.close()
            await server.wait_closed()
        remaining = list(tasks)
        for task in remaining:
            task.cancel()
        await asyncio.gather(*remaining, return_exceptions=True)


async def main(binary, only=None):
    cases = [
        ("stream-smoothness", "dayto2pe", 2, .025),
        ("stream-smoothness", "scud", 2, .025),
        ("stream-smoothness", "srally2", 2, .025),
        ("stream-smoothness", "lemans24", 2, .025),
        ("lemans24-predict", "lemans24", 2, .05),
        ("lemans24-predict", "lemans24", 3, .03),
        ("srally2-ring-echo", "srally2", 2, .05),
        ("srally2-ring-echo", "srally2", 4, .03),
        ("predict-cpu", "dayto2pe", 2, .1),
        ("predict-cpu", "dayto2pe", 3, .05),
        ("internet", "dayto2pe", 2, .1),
        ("internet", "dayto2pe", 3, .05),
        ("start-long-rtt", "dayto2pe", 2, .65),
        ("startup-deferred", "dayto2pe", 2, 0),
        ("start", "dayto2pe", 2, .1),
        ("start", "dayto2pe", 2, .15),
        ("start", "dayto2pe", 4, .075),
        ("start-fast", "dayto2pe", 2, .01),
        ("start-fast", "dayto2pe", 3, .02),
        ("start-stall", "dayto2pe", 2, .01),
        ("start-stall", "dayto2pe", 3, .01),
        ("start-late", "dayto2pe", 2, .1),
        ("start-repeat", "dayto2pe", 2, .05),
        ("start", "dayto2pe", 1, 0),
        ("start-auto", "dayto2pe", 2, .01),
        ("start-legacy-settings", "dayto2pe", 2, .01),
        ("start-async-off", "dayto2pe", 1, 0),
        ("start-other", "harley", 2, .01),
        ("start-mismatch", "dayto2pe", 2, .01),
        ("start-old-gate", "dayto2pe", 2, .01),
        ("start-timeout", "dayto2pe", 2, .01),
        ("start-malformed", "dayto2pe", 2, .01),
        ("start-checkpoint", "dayto2pe", 2, .01),
        ("start-disconnect", "dayto2pe", 2, .01),
        ("predict-rubberband", "dayto2pe", 1, 0),
        ("boot-scene", "dayto2pe", 1, 0),
        ("predict", "dayto2pe", 2, .05),
        ("predict", "dayto2pe", 3, .025),
        ("predict", "dayto2pe", 1, 0),
        ("predict-auto", "dayto2pe", 2, .01),
        ("predict-legacy-settings", "dayto2pe", 2, .01),
        ("predict-other", "harley", 2, .01),
        ("predict-size", "dayto2pe", 2, .01),
        ("sync", "harley", 1, 0),
        ("normal", "harley", 1, 0),
        ("normal", "harley", 2, 0),
        ("normal", "harley", 2, .1),
        ("normal", "scud", 4, .075),
        ("normal", "lemans24", 2, .15),
        ("relay", "von2", 3, .05),
        ("relay", "dirtdvls", 3, .05),
        ("zero", "harley", 2, .01),
        ("oversize", "harley", 2, .01),
        ("mismatch", "harley", 2, .01),
        ("mismatch-master", "harley", 2, .01),
        ("malformed", "harley", 2, .1),
        ("disconnect", "harley", 2, .1),
    ]
    if only:
        cases = [c for c in cases if c[0].startswith(only)]
    for args in cases:
        await case(binary, *args)
    print(f"PASS: all {len(cases)} scenarios")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--only", help="run only scenarios whose name starts with this prefix")
    args = parser.parse_args()
    asyncio.run(main(build(args.sanitize), args.only))
