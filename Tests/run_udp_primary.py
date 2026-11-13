#!/usr/bin/env python3
"""ROM-free UDP-primary race test: real relay.py (lobby, TCP ring, UDP lane)
plus the real HLE net board. Requires g++, SDL2/SDL2_net dev packages, Python 3.9+.
Run from anywhere: python3 Tests/run_udp_primary.py [--sanitize] [--tc] [--only SCENARIO]

The `netem` scenarios race under the impairment profiles in Tests/netem.py
(loss, bursts, jitter, duplicates). --tc additionally puts a kernel netem
qdisc on loopback for them when available (Linux, CAP_NET_ADMIN).
"""
import os
import random
import argparse
import asyncio
import shlex
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "Tests"))
import relay
import netem
from run_online_lobby import Client

online = relay.lobby
SCUD = [name for name, _ in online.GAMES].index("scud")
SRALLY2 = [name for name, _ in online.GAMES].index("srally2")


def build(sanitize):
    output = ROOT / "build" / "udp-primary-test"
    output.parent.mkdir(exist_ok=True)
    sources = ["Tests/UdpPrimaryTest.cpp", "Src/Model3/SimNetBoard.cpp",
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
           "-ISrc/OSD/SDL", "-ISrc/Util", "-ITests"]
    if sanitize:
        cmd += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie"]
    subprocess.run(cmd + sources + sdl + ["-lSDL2_net", "-o", str(output)], cwd=ROOT, check=True)
    return output


class Filter:
    """Wraps the relay's UDP transport; can stop snapshot delivery to one cabinet, drop a share of all
    snapshots, or run them through a netem.Impairment (loss bursts, jitter, reordering, duplicates)."""
    def __init__(self, udp, transport, profile=None):
        self.udp, self.transport, self.blocked, self.loss = udp, transport, None, 0.0
        self.random = random.Random(1)
        self.impairment = netem.Impairment(profile, transport.sendto) if profile is not None else None

    def sendto(self, packet, address):
        if self.loss and packet[5] == relay.relay_udp.SNAPSHOT and self.random.random() < self.loss:
            return
        if self.blocked is not None and packet[5] == relay.relay_udp.SNAPSHOT:
            session = self.udp.sessions.get(packet[8:24])
            if session is not None and session.index == self.blocked:
                return
        if self.impairment is not None and packet[5] == relay.relay_udp.SNAPSHOT:
            self.impairment.offer(packet, address)
            return
        self.transport.sendto(packet, address)

    def __getattr__(self, name):
        return getattr(self.transport, name)


async def case(binary, scenario, n, tick_rate=20, latency_ms=0, profile=None, tc=False):
    # Lobby clients advertise star fan-out (bit 64) like the real GUI. The ring*
    # scenarios join as older clients so the relay keeps the hop-by-hop ring, and
    # so does `latency`: it checks the merge of late TCP control words, which
    # needs TCP to lag UDP by more than the one-way delay.
    star = not scenario.startswith("ring") and scenario != "latency" and "ring" not in scenario
    game = "srally2" if scenario.startswith("srally2") else "scud"
    logs = []
    # Under an impairment profile the relay's fake latency carries the TCP
    # delay/jitter (order preserving, like TCP); the Filter's Impairment does
    # the UDP side itself, so the relay's UDP lane gets no fake latency.
    # With a kernel qdisc both lanes cross the real impaired loopback instead.
    kernel = netem.kernel_netem(profile) if (profile is not None and tc) else None
    kernel_active = kernel.__enter__() if kernel is not None else False
    if profile is not None and not kernel_active:
        latency_ms = profile.delay_ms
    jitter_ms = profile.jitter_ms if (profile is not None and not kernel_active) else 0
    legacy = relay.Room(log=logs.append, tick_rate=tick_rate, latency_ms=latency_ms, jitter_ms=jitter_ms)
    hub = online.Hub(legacy, log=logs.append, tick_rate=tick_rate, latency_ms=latency_ms, jitter_ms=jitter_ms)
    server = await asyncio.start_server(hub.handle, "127.0.0.1", 0)
    port = server.sockets[0].getsockname()[1]
    udp = flt = None
    if scenario != "tcp-only" and not scenario.startswith(("ring", "star")):  # relay default: UDP lane on the same port number
        udp = relay.relay_udp.UdpRelay(None if profile is not None else legacy.latency)
        await asyncio.get_running_loop().create_datagram_endpoint(lambda: udp, local_addr=("127.0.0.1", port))
        flt = udp.transport = Filter(udp, udp.transport, None if kernel_active else profile)
        if scenario == "fallback":
            flt.blocked = 1
    legacy.udp = udp
    hub.start()
    env = dict(os.environ)
    if profile is not None:
        env.update(NETEM_PROFILE=profile.name, NETEM_MAX_STALE_MS=str(profile.max_stale_ms), NETEM_MAX_DROPS=str(profile.max_drops))
    clients = [await Client().connect(port, name=f"P{i}", enabled=65 if star else 1) for i in range(n)]
    proc = None
    try:
        room = await clients[0].create(SRALLY2 if game == "srally2" else SCUD)
        for c in clients[1:]:
            await c.join(room)
        clients[0].send(b"OLT1", struct.pack("<I", room))
        tickets = [online.LAUNCH.unpack(await c.until(b"OLG1"))[4].hex() for c in clients]
        for c in clients:
            c.close()
        print(f"TEST {scenario} {game} cabinets={n} tick={tick_rate} Hz" + (f" latency={latency_ms} ms one-way" if latency_ms else "")
              + (" star fan-out" if star else " ring")
              + (f" profile={profile.name} ({profile.describe()})" + (" via kernel netem" if kernel_active else "") if profile else ""), flush=True)
        proc = await asyncio.create_subprocess_exec(str(binary), scenario, game, f"127.0.0.1:{port}", *tickets,
                                                    cwd=ROOT, stdout=asyncio.subprocess.PIPE, env=env)

        measured = {}

        async def pump():
            while True:
                line = await proc.stdout.readline()
                if not line:
                    return
                text = line.decode().rstrip()
                if text.startswith("STAT "):  # key=value pairs reported by the test binary
                    measured.update(kv.split("=", 1) for kv in text.split()[1:])
                elif text == "CTL block":
                    flt.blocked = 1
                elif text == "CTL unblock":
                    flt.blocked = None
                elif text.startswith("CTL loss "):
                    flt.loss = float(text.split()[2])
                elif text.startswith("PASS") or text.startswith("INFO"):
                    print(text, flush=True)
        await asyncio.wait_for(pump(), 90)
        if await proc.wait():
            print("\n".join(str(x) for x in logs[-15:]))
            raise RuntimeError(f"{scenario} n={n} exited {proc.returncode}")
        if udp is not None:
            avg = udp.bytes_in / udp.accepted if udp.accepted else 0
            print(f"     relay UDP: accepted={udp.accepted} forwarded={udp.forwarded} dropped={udp.dropped} "
                  f"bytes_in={udp.bytes_in} bytes_out={udp.bytes_out} avg_datagram={avg:.0f} B", flush=True)
        if flt is not None and flt.impairment is not None:
            c = flt.impairment.counters
            seen, lost = int(measured.get("datagrams", 0)), int(measured.get("lost", 0))
            print(f"     impairment: offered={c.offered} dropped={c.dropped} ({c.loss * 100:.1f}%) duplicated={c.duplicated} "
                  f"reordered={c.reordered}; cabinets counted {seen} snapshots, {lost} lost "
                  f"({100.0 * lost / (seen + lost) if seen + lost else 0:.1f}%), {measured.get('reordered', 0)} reordered, "
                  f"{measured.get('duplicated', 0)} duplicated", flush=True)
            # The receivers' sequence accounting must agree with what the rig really dropped.
            if c.offered >= 200 and abs(lost - c.dropped) > max(10, c.dropped // 4):
                raise RuntimeError(f"{scenario} {profile.name}: cabinets counted {lost} lost snapshots, rig dropped {c.dropped}")
    finally:
        if kernel is not None:
            kernel.__exit__(None, None, None)
        if proc is not None and proc.returncode is None:
            proc.kill()
            await proc.wait()
        for c in clients:
            c.close()
        server.close()
        await server.wait_closed()
        await hub.close()
        await legacy.close()
        if udp is not None:
            if flt is not None and flt.impairment is not None:
                flt.impairment.close()
            udp.close()


async def main(binary, only=None, tc=False):
    cases = [("primary", 2), ("primary", 4), ("control", 3), ("latency", 3, 20, 80), ("stutter", 3), ("recover", 3), ("fallback", 3), ("tcp-only", 2),
             ("ring", 4), ("ring-latency", 4, 20, 40), ("star", 4), ("star-latency", 4, 20, 40)]
    cases += [("netem", 3, 20, 0, profile) for profile in netem.PROFILES.values()]
    cases += [("srally2-star", 4, 60), ("srally2-ring", 4, 60), ("srally2-star-latency", 4, 60, 40), ("srally2-ring-latency", 4, 60, 40)]
    if only:
        cases = [c for c in cases if c[0] == only or (c[0] == "netem" and c[4].name == only)]
    for args in cases:
        await case(binary, *args, tc=tc)
    print(f"PASS: all {len(cases)} UDP-primary scenarios")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--tc", action="store_true", help="use a kernel netem qdisc on loopback for the netem scenarios when available")
    parser.add_argument("--only", help="run one scenario (or one netem profile) by name")
    args = parser.parse_args()
    asyncio.run(main(build(args.sanitize), args.only, args.tc))
