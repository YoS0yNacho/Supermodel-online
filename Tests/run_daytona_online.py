#!/usr/bin/env python3
"""ROM-free Daytona Rev A GUI admission, native ring and optional pose lane."""
# SPDX-License-Identifier: GPL-3.0-or-later
import asyncio
import struct
import time

from run_indy500_online import StreamTransport, read
from run_indy_pose_udp import Inbox
from run_indy_room_options import await_view, launch
from run_online_lobby import Client, board, online, relay, wait_for


async def check(count, latency, stream=False, udp=True, mixed=False):
    loop = asyncio.get_running_loop()
    lobby_game = online.DAYTONA_GAME
    legacy = relay.Room(log=lambda _: None)
    if udp:
        _, legacy.udp = await loop.create_datagram_endpoint(relay.udp.UdpRelay,
                                                             local_addr=('127.0.0.1', 0))
    hub = online.Hub(legacy, log=lambda _: None, latency_ms=latency, jitter_ms=latency//4)

    async def handler(reader, writer):
        if stream:
            writer._transport = StreamTransport(writer.transport)
        await hub.handle(reader, writer)

    server = await asyncio.start_server(handler, '127.0.0.1', 0)
    port = server.sockets[0].getsockname()[1]
    hub.start()
    clients, links, sockets, poses = [], {}, [], {}
    hello, reply = online.DAYTONA_HELLO, online.DAYTONA_RELAY_HELLO

    async def lobby(name, greeting=hello+b'0', enabled=511, mask=0):
        c = await Client().connect(port, name=name, hello=greeting, enabled=enabled, mask=mask)
        clients.append(c)
        return c

    async def channel(ticket, n, **kwargs):
        r, w = await board(port, lobby_game, ticket, n, **kwargs)
        sockets.append(w)
        return r, w

    async def rejected(ticket, **kwargs):
        r, w = await channel(ticket, 0, **kwargs)
        assert (await read(r))[0] == b'HEL1' and (await read(r))[0] == b'RLE1'
        w.close()

    try:
        host = await lobby('Car 1', hello+b'3')
        assert host.hello == reply+b'3'
        old = await lobby('Old Rev C client', online.BOTH_MODEL2_HELLO, mask=511)
        assert old.hello == online.BOTH_MODEL2_RELAY_HELLO
        no_exact = await lobby('No exact supported ROM', b'Supermodel Online', mask=511)
        no_options = await lobby('No native settings', enabled=1)
        malformed = await lobby('Unknown greeting flag', hello+b'4')
        for c in (no_exact, no_options, malformed):
            assert c.hello == b'Supermodel Online lobby v1'
        title = online.fixed('Test Daytona', 64)
        for c in (old, no_exact, no_options, malformed):
            c.send(b'OLC1', online.CREATE.pack(lobby_game, title))
            assert b'ROM' in await c.until(b'OLE1')
        for kind, mode in ((b'OLC2', 1), (b'OLC3', 2), (b'OLC4', 3)):
            host.send(kind, online.CREATE2.pack(lobby_game, title, mode, 1971))
            why = b'--tcp-only' if not udp and mode==1 else b'central TCP relay'
            assert why in await host.until(b'OLE1')
        room = await host.create(lobby_game)
        for c in (old, no_exact):
            c.send(b'OLQ1'); assert await c.snapshot() == []
            c.send(b'OLJ1',struct.pack('<I',room)); assert b'ROM' in await c.until(b'OLE1')
        other = await old.create(online.SRALLYC_GAME)
        host.send(b'OLQ1'); assert {r[0] for r in await host.snapshot()} == {room, other}
        await await_view(host, room, (online.DAYTONA_DEFAULT_OPTIONS,1))
        host.send(b'OLT1',struct.pack('<I',room)); assert b'2-8' in await host.until(b'OLE1')
        seats = [host]
        for i in range(1,count):
            suffix=bytes((ord('0')+i%4,))
            c = await lobby(f'Car {i+1}',hello+suffix)
            assert c.hello == reply+suffix
            await c.join(room); seats.append(c)
        if count == 8:
            ninth = await lobby('Ninth car')
            ninth.send(b'OLJ1',struct.pack('<I', room))
            assert b'full' in await ninth.until(b'OLE1')
        opts = 2 | (3 << 2) | (1 << 5)
        for wrong in (0,1,2|4<<2,2|3<<5,opts|2048,opts|8192):
            host.send(b'ORO1',struct.pack('<II',room,wrong)); assert b'valid room options' in await host.until(b'OLE1')
        seats[-1].send(b'ORO1',struct.pack('<II',room,opts))
        assert b'Only the master' in await seats[-1].until(b'OLE1')
        host.send(b'OCN1',struct.pack('<II',room,9)); assert b'CAR NUMBER' in await host.until(b'OLE1')
        host.send(b'ORO1',struct.pack('<II',room,opts))
        await asyncio.gather(*(await_view(c,room,(opts,1)) for c in seats))
        seats[-1].send(b'OLT1',struct.pack('<I',room)); assert b'Only' in await seats[-1].until(b'OLE1')
        host.send(b'OLT1',struct.pack('<I',room))
        tickets = []
        cars_seen = []
        for i,c in enumerate(seats):
            payload = await launch(c,b'OLG5')
            ident, game, index, n, ticket = online.LAUNCH.unpack_from(payload)
            assert (ident,game,index,n)==(room,lobby_game,i,count)
            options_now, car_now = struct.unpack_from('<II',payload,online.LAUNCH.size)
            assert options_now==opts and 1<=car_now<=8
            cars_seen.append(car_now)
            tickets.append(ticket)
        assert len(set(tickets))==count and bytes(16) not in tickets
        assert len(set(cars_seen))==count
        host.send(b'ORO1',struct.pack('<II',room,6)); assert b'before launch' in await host.until(b'OLE1')
        for c in seats: c.close()
        await rejected(bytes(16))
        await rejected(tickets[0],bad_guid=relay.INDY500_GUID)
        await rejected(tickets[0],registration=b'RLJ3')
        active = udp and not mixed
        for i in reversed(range(count)):
            h = b'older Daytona client' if mixed and i==count-1 else relay.DAYTONA_POSE_HELLO
            for n in (1,0): links[i,n]=await channel(tickets[i],n,hello=h)
        for (i,n),(r,_) in links.items():
            assert (await read(r))[0] == b'HEL1'
            kind,payload,_ = await read(r)
            if active and n==0:
                assert kind==b'RLU1'
                v, udp_port, index, size, token = struct.unpack('<IIII16s',payload)
                assert (v, udp_port,index,size)==(1,legacy.udp.port,i,count)
                t,inbox=await loop.create_datagram_endpoint(Inbox,remote_addr=('127.0.0.1',udp_port))
                poses[i]=(t,inbox,token)
                t.sendto(relay.udp.HEADER.pack(b'UGP1',relay.udp.WIRE_VERSION,relay.udp.HELLO,
                                              0,0,token,1,i,0,time.monotonic_ns()//1000))
                ack=relay.udp.parse(await inbox.receive())
                assert ack[2]==relay.udp.ACK and ack[5]==token
                kind,payload,_=await read(r)
            assert kind==b'RLA1' and relay.READY.unpack(payload)==(1,i,count,n)
        bound=hub.rooms[room].board
        assert bool(bound.udp_sessions)==active and not bound.indy_race_udp and not bound.star_active and bound.adaptive_ctl is None
        assert all((c.proto is None)==stream for chans in bound.players.values() for c in chans.values())
        await rejected(tickets[0])
        assert not legacy.started and not legacy.players
        if active:
            raw=bytearray(relay.DAYTONA_POSE_WIRE)
            struct.pack_into('<4sIIBBBBffH',raw,0,b'DYP1',1,1,1,0,77,1,10.25,-12.5,0x4000)
            struct.pack_into('<Q',raw,32,0x1122446688aaccee)
            payload=b'\0'+raw+bytes(40)
            t,_,token=poses[0]
            t.sendto(relay.udp.HEADER.pack(b'UGP1',relay.udp.WIRE_VERSION,relay.udp.SNAPSHOT,
                                          0,1,token,2,0,len(payload),time.monotonic_ns()//1000)+payload)
            for i in range(1,count):
                packet=await poses[i][1].receive()
                parsed=relay.udp.parse(packet)
                assert parsed[5]==poses[i][2] and parsed[7]==0 and packet[relay.udp.HEADER.size:]==payload
            assert legacy.udp.accepted==1 and legacy.udp.forwarded==count-1

        async def ring(i):
            w=links[i,0][1]; r=links[(i-1)%count,1][0]
            frames=[relay.frame(b'NBD1',bytes((tag,j))+bytes((i,))*3583)
                    for j,tag in enumerate((0xff,0xfe,i+1,0xfc,i+1,0xfc))]
            w.write(frames[0][:3]); await w.drain(); await asyncio.sleep(.02)
            w.write(frames[0][3:]+b''.join(frames[1:])); await w.drain()
            for expected in frames: assert (await read(r))[2]==expected
        await asyncio.gather(*(ring(i) for i in range(count)))
        links[0,0][1].write(relay.frame(b'NBD1',bytes(2048)))
        await wait_for(lambda:bound.failed and hub.rooms[room].state==3)
        if udp: assert not legacy.udp.sessions
        print(f'PASS Daytona {count} seats: ordered full TCP, '+('pose UDP' if active else 'mixed/opt-out' if mixed else 'relay TCP-only')+
              (', stream fallback' if stream else ', buffered jitter' if latency else ', fast forwarding')+', admission/settings/teardown')
    finally:
        for t,_,__ in poses.values(): t.close()
        for w in sockets: w.close()
        for c in clients: c.close()
        server.close(); await hub.close(); await legacy.close(); await server.wait_closed()
        if udp: legacy.udp.close()


async def main():
    assert online.GAMES[11]==('daytona',0x5bf177da34880)
    assert online.max_players_for_game(11)==relay.DAYTONA_MAX_CABINETS==8
    assert relay.nbd1_wire_sizes(relay.DAYTONA_GUID)=={3593}
    base={2 | d<<2 | m<<5 for d in range(4) for m in range(3)}
    # Daytona settings: No Collisions (256), Real Players (512), No Pitstop (4096).
    extras={e for e in range(1<<13) if e & ~(256|512|4096)==0}
    valid={v | e for v in base for e in extras}
    valid |= {v | relay.VIDEO60_OPTION for v in valid}
    assert {v for v in range(1<<14) if online.valid_room_options(11,v)}==valid
    for n in range(2,9): await check(n,0)
    await check(4,35,mixed=True)
    await check(2,30,udp=False)
    await check(8,30,stream=True)


if __name__=='__main__':
    asyncio.run(main())
