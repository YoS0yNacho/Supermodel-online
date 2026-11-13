#!/usr/bin/env python3
"""ROM-free UDP-primary race test: real relay.py (lobby, TCP ring, UDP lane)
plus the real HLE net board. Requires g++, SDL2/SDL2_net dev packages, Python 3.9+.
Run from anywhere: python3 Tests/run_udp_primary.py [--sanitize]
"""
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
from run_online_lobby import Client

online = relay.lobby
SCUD = [name for name, _ in online.GAMES].index("scud")


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
    """Wraps the relay's UDP transport; can stop snapshot delivery to one cabinet."""
    def __init__(self, udp, transport):
        self.udp, self.transport, self.blocked = udp, transport, None

    def sendto(self, packet, address):
        if self.blocked is not None and packet[5] == relay.relay_udp.SNAPSHOT:
            session = self.udp.sessions.get(packet[8:24])
            if session is not None and session.index == self.blocked:
                return
        self.transport.sendto(packet, address)

    def __getattr__(self, name):
        return getattr(self.transport, name)


async def case(binary, scenario, n, tick_rate=20):
    logs = []
    legacy = relay.Room(log=logs.append, tick_rate=tick_rate)
    hub = online.Hub(legacy, log=logs.append, tick_rate=tick_rate)
    server = await asyncio.start_server(hub.handle, "127.0.0.1", 0)
    port = server.sockets[0].getsockname()[1]
    udp = flt = None
    if scenario != "tcp-only":  # relay default: UDP lane on the same port number
        udp = relay.relay_udp.UdpRelay(legacy.latency)
        await asyncio.get_running_loop().create_datagram_endpoint(lambda: udp, local_addr=("127.0.0.1", port))
        flt = udp.transport = Filter(udp, udp.transport)
        if scenario == "fallback":
            flt.blocked = 1
    legacy.udp = udp
    hub.start()
    clients = [await Client().connect(port, name=f"P{i}") for i in range(n)]
    proc = None
    try:
        room = await clients[0].create(SCUD)
        for c in clients[1:]:
            await c.join(room)
        clients[0].send(b"OLT1", struct.pack("<I", room))
        tickets = [online.LAUNCH.unpack(await c.until(b"OLG1"))[4].hex() for c in clients]
        for c in clients:
            c.close()
        print(f"TEST {scenario} scud cabinets={n} tick={tick_rate} Hz", flush=True)
        proc = await asyncio.create_subprocess_exec(str(binary), scenario, "scud", f"127.0.0.1:{port}", *tickets,
                                                    cwd=ROOT, stdout=asyncio.subprocess.PIPE)

        async def pump():
            while True:
                line = await proc.stdout.readline()
                if not line:
                    return
                text = line.decode().rstrip()
                if text == "CTL block":
                    flt.blocked = 1
                elif text == "CTL unblock":
                    flt.blocked = None
                elif text.startswith("PASS"):
                    print(text, flush=True)
        await asyncio.wait_for(pump(), 90)
        if await proc.wait():
            print("\n".join(str(x) for x in logs[-15:]))
            raise RuntimeError(f"{scenario} n={n} exited {proc.returncode}")
        if udp is not None:
            print(f"     relay UDP: accepted={udp.accepted} forwarded={udp.forwarded} dropped={udp.dropped}", flush=True)
    finally:
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
            udp.close()


async def main(binary):
    cases = [("primary", 2), ("primary", 4), ("recover", 3), ("fallback", 3), ("tcp-only", 2)]
    for args in cases:
        await case(binary, *args)
    print(f"PASS: all {len(cases)} UDP-primary scenarios")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sanitize", action="store_true")
    asyncio.run(main(build(parser.parse_args().sanitize)))
