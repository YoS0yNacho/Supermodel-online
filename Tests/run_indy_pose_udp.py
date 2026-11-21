#!/usr/bin/env python3
"""ROM-free loopback check: optional pose fan-out alongside native INDY TCP."""
# SPDX-License-Identifier: GPL-3.0-or-later
import asyncio
import struct
import time
from run_online_lobby import Client, board, online, relay


class Inbox(asyncio.DatagramProtocol):
    def __init__(self):
        self.queue = asyncio.Queue()

    def datagram_received(self, data, _address):
        self.queue.put_nowait(data)

    async def receive(self):
        return await asyncio.wait_for(self.queue.get(), 2)


async def read(r):
    return await asyncio.wait_for(relay.read_frame(r), 3)


async def check(udp_enabled):
    loop = asyncio.get_running_loop()
    legacy = relay.Room(log=lambda _: None)
    if udp_enabled:
        _, legacy.udp = await loop.create_datagram_endpoint(relay.udp.UdpRelay, local_addr=('127.0.0.1', 0))
    hub = online.Hub(legacy, log=lambda _: None)
    server = await asyncio.start_server(hub.handle, '127.0.0.1', 0)
    port = server.sockets[0].getsockname()[1]
    hub.start()
    clients, boards, sockets = [], {}, []
    try:
        for i in range(3):
            clients.append(await Client().connect(port, f'Cabinet {i+1}', hello=online.INDY500_HELLO))
        room = await clients[0].create(online.INDY500_GAME)
        for client in clients[1:]:
            await client.join(room)
        clients[0].send(b'OLT1', struct.pack('<I', room))
        tickets = [online.LAUNCH.unpack(await c.until(b'OLG1'))[4] for c in clients]
        hello = (relay.INDY500_POSE_HELLO, b'original client', relay.INDY500_POSE_HELLO)
        for i in (2,1,0):
            for ch in (1,0):
                boards[i,ch] = await board(port, online.INDY500_GAME, tickets[i], ch, hello=hello[i])
        cookies = {}
        for (i,ch),(r,_w) in boards.items():
            assert (await read(r))[0] == b'HEL1'
            tag, payload, _ = await read(r)
            if udp_enabled and i!=1 and ch==0:
                assert tag == b'RLU1',tag
                v, udp_port, index, count, token = struct.unpack('<IIII16s',payload)
                assert (v,udp_port,index,count) == (1,legacy.udp.port,i,3)
                cookies[i] = token
                tag,payload,_ = await read(r)
            assert tag == b'RLA1' and struct.unpack('<IIII',payload)==(1,i,3,ch)
        if udp_enabled:
            endpoints = {}
            for i,token in cookies.items():
                t,inbox = await loop.create_datagram_endpoint(Inbox, remote_addr=('127.0.0.1',legacy.udp.port))
                sockets.append(t)
                endpoints[i]=(t,inbox)
                t.sendto(relay.udp.HEADER.pack(b'UGP1', relay.udp.WIRE_VERSION, relay.udp.HELLO,
                                             0,0,token,1,i,0,time.monotonic_ns()//1000))
                ack=relay.udp.parse(await inbox.receive())
                assert ack[2] == relay.udp.ACK and ack[5] == token
            # Normal full (not delta) one-datagram pose bundle. The relay keeps
            # the payload opaque and attaches only the destination's cookie.
            pose=struct.pack('<4sIIIBBBBffH',b'IPU1',1,1,50,7,1,1,1,34.5,-85.75,123)
            pose+=bytes(relay.INDY500_POSE_WIRE-len(pose))
            payload=b'\x00'+pose+bytes(40)
            tx=endpoints[0][0]
            tx.sendto(relay.udp.HEADER.pack(b'UGP1',relay.udp.WIRE_VERSION,relay.udp.SNAPSHOT,
                                          0,1,cookies[0],2,0,len(payload),time.monotonic_ns()//1000)+payload)
            delivered=await endpoints[2][1].receive()
            head=relay.udp.parse(delivered)
            assert head[5]==cookies[2] and head[7]==0 and delivered[relay.udp.HEADER.size:]==payload
            assert legacy.udp.accepted==legacy.udp.forwarded==1
        # Even with UDP active, native packets must arrive intact, in order,
        # by the original decreasing-ID ring, including at the old client.
        for i in range(3):
            payloads = [bytes((k,i))+bytes([i+23])*(relay.INDY500_PAYLOAD-2) for k in (0xff,0xfe,0xfd,i+1,0xfc)]
            boards[i,0][1].write(b''.join(relay.frame(b'NBD1',p) for p in payloads))
            for p in payloads:
                tag,body,_ = await read(boards[(i+2)%3,1][0])
                assert tag==b'NBD1' and body==p
        print(f'PASS Indy pose UDP {"on, mixed clients and opaque fan-out" if udp_enabled else "disabled at relay"}: native ordered pairs unchanged')
    finally:
        for t in sockets: t.close()
        for _r,w in boards.values(): w.close()
        for c in clients: c.close()
        server.close(); await hub.close(); await legacy.close(); await server.wait_closed()
        if legacy.udp is not None: legacy.udp.close()


async def main():
    await check(True)
    await check(False)


if __name__=='__main__':
    asyncio.run(main())
