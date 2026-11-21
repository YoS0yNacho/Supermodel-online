#!/usr/bin/env python3
"""Optional Rev C pose lane and its byte-exact native TCP fallback (ROM-free)."""
# SPDX-License-Identifier: GPL-3.0-or-later
import asyncio
import struct
import time

from run_indy_pose_udp import Inbox, read
from run_indy_room_options import launch
from run_online_lobby import Client, board, online, relay, wait_for


async def check(count, enabled, mixed=False):
    loop = asyncio.get_running_loop()
    legacy = relay.Room(log=lambda _: None)
    if enabled:
        _, legacy.udp = await loop.create_datagram_endpoint(relay.udp.UdpRelay, local_addr=('127.0.0.1', 0))
    hub = online.Hub(legacy, log=lambda _: None)
    server = await asyncio.start_server(hub.handle, '127.0.0.1', 0)
    port = server.sockets[0].getsockname()[1]
    hub.start()
    clients, links, udp_clients = [], {}, {}
    active = enabled and not mixed
    try:
        for i in range(count):
            clients.append(await Client().connect(port, f'Car {i+1}', hello=online.SRALLYC_HELLO, enabled=511))
        room = await clients[0].create(online.SRALLYC_GAME)
        for c in clients[1:]:
            await c.join(room)
        clients[0].send(b'OLT1', struct.pack('<I', room))
        tickets = [online.LAUNCH.unpack_from(await launch(c, b'OLG5'))[4] for c in clients]
        for i in reversed(range(count)):
            hello = b'older Rev C client' if mixed and i==count-1 else relay.SRALLYC_POSE_HELLO
            for ch in (1, 0):
                links[i,ch] = await board(port, online.SRALLYC_GAME, tickets[i], ch, hello=hello)
        for (i,ch),(r,_) in links.items():
            assert (await read(r))[0] == b'HEL1'
            kind, payload, _ = await read(r)
            if active and ch==0:
                assert kind == b'RLU1'
                v, udp_port, index, n, token = struct.unpack('<IIII16s',payload)
                assert (v, udp_port, index, n) == (1, legacy.udp.port, i, count)
                t, inbox = await loop.create_datagram_endpoint(Inbox, remote_addr=('127.0.0.1', udp_port))
                udp_clients[i]=(t,inbox,token)
                t.sendto(relay.udp.HEADER.pack(b'UGP1', relay.udp.WIRE_VERSION, relay.udp.HELLO,
                                             0,0,token,1,i,0,time.monotonic_ns()//1000))
                ack=relay.udp.parse(await inbox.receive())
                assert ack[2] == relay.udp.ACK and ack[5] == token
                kind,payload,_ = await read(r)
            assert kind==b'RLA1' and relay.READY.unpack(payload)==(1,i,count,ch)
        room_board = hub.rooms[room].board
        assert bool(room_board.udp_sessions)==active and not room_board.indy_race_udp and not room_board.star_active
        if active:
            assert len(udp_clients)==count
            pose = bytearray(relay.SRALLYC_POSE_WIRE)
            struct.pack_into('<4sIIIIBB',pose,0,b'SRP1',17,0,1,8,1,1)
            struct.pack_into('<5f',pose,24,2.5,-28.25,0.38,0.5,-0.75)
            struct.pack_into('<Q',pose,48,0x1122334455667788)
            payload=b'\x00'+pose+bytes(40)
            t,_,token=udp_clients[0]
            t.sendto(relay.udp.HEADER.pack(b'UGP1',relay.udp.WIRE_VERSION,relay.udp.SNAPSHOT,
                                          0,1,token,2,0,len(payload),time.monotonic_ns()//1000)+payload)
            for i in range(1,count):
                packet=await udp_clients[i][1].receive()
                parsed=relay.udp.parse(packet)
                assert parsed[5]==udp_clients[i][2] and parsed[7]==0 and packet[relay.udp.HEADER.size:]==payload
            assert legacy.udp.accepted==1 and legacy.udp.forwarded==count-1
        for i in range(count):
            native = [relay.frame(b'NBD1', bytes((k,i))+bytes([20+i])*3583) for k in (0xff,0xfe,i+1,0xfc,i+1,0xfc)]
            links[i,0][1].write(b''.join(native))
            for expected in native:
                assert (await read(links[(i-1)%count,1][0]))[2] == expected
        links[count-1,0][1].close()
        await wait_for(lambda: room_board.failed)
        if active:
            assert not legacy.udp.sessions
        mode = 'pose UDP' if active else 'previous/opted-out client' if mixed else 'relay TCP-only'
        print(f'PASS Rev C {count} seats {mode}: versioned negotiation, exact full TCP frames and teardown')
    finally:
        for t,_,__ in udp_clients.values(): t.close()
        for _,w in links.values(): w.close()
        for c in clients: c.close()
        server.close(); await hub.close(); await legacy.close(); await server.wait_closed()
        if legacy.udp is not None: legacy.udp.close()


async def main():
    for n in (2,3,4): await check(n, True)
    await check(3, True, mixed=True)
    await check(2, False)


if __name__=='__main__':
    asyncio.run(main())
