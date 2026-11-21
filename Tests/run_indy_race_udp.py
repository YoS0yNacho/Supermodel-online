#!/usr/bin/env python3
"""ROM-free: Indy primary capability, live UDP fan-out, ordered TCP, old peers and --tcp-only."""
# SPDX-License-Identifier: GPL-3.0-or-later
import asyncio
import struct
import time

from run_indy_pose_udp import Inbox, read
from run_online_lobby import Client, board, online, relay


async def exercise(profile):
    enabled = profile != 'tcp-only'
    split = profile in ('fast', 'delayed')
    loop = asyncio.get_running_loop()
    legacy = relay.Room(log=lambda _: None)
    if enabled:
        _, legacy.udp = await loop.create_datagram_endpoint(relay.udp.UdpRelay, local_addr=('127.0.0.1', 0))
    hub = online.Hub(legacy, log=lambda _: None, latency_ms=6 if profile == 'delayed' else 0)
    server = await asyncio.start_server(hub.handle, '127.0.0.1', 0)
    port = server.sockets[0].getsockname()[1]
    hub.start()
    clients, boards, udp = [], {}, []
    count = 3
    try:
        for i in range(count):
            clients.append(await Client().connect(port, f'Cabinet {i+1}', hello=online.INDY500_HELLO))
        room = await clients[0].create(online.INDY500_GAME)
        for c in clients[1:]:
            await c.join(room)
        clients[0].send(b'OLT1', struct.pack('<I', room))
        tickets = [online.LAUNCH.unpack(await c.until(b'OLG1'))[4] for c in clients]
        for i in reversed(range(count)):
            hello = relay.INDY500_POSE_HELLO if profile == 'mixed' and i == 1 else relay.INDY500_RACE_HELLO
            for ch in (1, 0):
                boards[i, ch] = await board(port, online.INDY500_GAME, tickets[i], ch, hello=hello)
        endpoints = {}
        for (i, ch), (r, _) in boards.items():
            assert (await read(r))[0] == b'HEL1'
            kind, payload, _ = await read(r)
            if enabled and ch == 0:
                assert kind == (b'RLUI' if split else b'RLU1'), kind
                version, udp_port, index, size, cookie = struct.unpack('<IIII16s', payload)
                assert (version, udp_port, index, size) == (1, legacy.udp.port, i, count)
                tx, rx = await loop.create_datagram_endpoint(Inbox, remote_addr=('127.0.0.1', udp_port))
                udp.append(tx)
                tx.sendto(relay.udp.HEADER.pack(b'UGP1', relay.udp.WIRE_VERSION, relay.udp.HELLO,
                                               0, 0, cookie, 1, i, 0, time.monotonic_ns()//1000))
                ack = relay.udp.parse(await rx.receive())
                assert ack[2] == relay.udp.ACK and ack[5] == cookie
                endpoints[i] = (tx, rx, cookie)
                kind, payload, _ = await read(r)
            assert kind == b'RLA1' and struct.unpack('<IIII', payload) == (1, i, count, ch)
        assert hub.rooms[room].board.indy_race_udp == split
        if split:
            tx, _, cookie = endpoints[0]
            movement = struct.pack('<4sIIIBBBBfffhhhh', b'IRM1', 1, 13, 65537, 7, 1, 1, 1,
                                   1.625, 2.765, 3.875, 111, -442, 9004, 1)
            movement += bytes(relay.INDY500_RACE_WIRE - len(movement))
            payload = b'\0' + movement + bytes(40)
            stamp = time.monotonic_ns() // 1000
            tx.sendto(relay.udp.HEADER.pack(b'UGP1', relay.udp.WIRE_VERSION, relay.udp.SNAPSHOT,
                                           0, 1, cookie, 2, 0, len(payload), stamp) + payload)
            for dest in (1, 2):
                _, rx, dest_cookie = endpoints[dest]
                datagram = await rx.receive()
                head = relay.udp.parse(datagram)
                assert head[5] == dest_cookie and head[7] == 0 and datagram[relay.udp.HEADER.size:] == payload
            assert legacy.udp.accepted == 1 and legacy.udp.forwarded == 2
        native = lambda tag, i: bytes((tag, i)) + bytes((50 + i,)) * (relay.INDY500_PAYLOAD - 2)
        pair = bytes((1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 255))
        for i in range(count):
            r = boards[(i-1) % count, 1][0]
            w = boards[i, 0][1]
            messages = [(b'NBD1', native(0xff, i)), (b'NBD1', native(i + 1, i)), (b'NBD1', native(0xfc, i))]
            if split:
                messages.insert(1, (b'IRP1', pair))
            data = b''.join(relay.frame(kind, body) for kind, body in messages)
            w.write(data[:3]); await w.drain(); await asyncio.sleep(.001)
            w.write(data[3:]); await w.drain()
            for tag, payload in messages:
                kind, body, _ = await read(r)
                assert (kind, body) == (tag, payload)
        # Bound compact framing to the negotiated room, even over the fast path.
        boards[0, 0][1].write(relay.frame(b'IRP1', b'bad' if split else pair))
        deadline = loop.time() + 2
        while not hub.rooms[room].board.failed and loop.time() < deadline:
            await asyncio.sleep(.01)
        assert hub.rooms[room].board.failed
        print(f'PASS Indy race UDP {profile}: negotiated codec, latest motion fan-out when enabled, ordered native control and full fallback framing')
    finally:
        for sock in udp:
            sock.close()
        for _, w in boards.values():
            w.close()
        for c in clients:
            c.close()
        server.close()
        await hub.close(); await legacy.close(); await server.wait_closed()
        if enabled:
            legacy.udp.close()


async def main():
    for profile in ('fast', 'delayed', 'mixed', 'tcp-only'):
        await exercise(profile)


if __name__ == '__main__':
    asyncio.run(main())
