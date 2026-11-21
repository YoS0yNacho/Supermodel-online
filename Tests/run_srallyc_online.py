#!/usr/bin/env python3
"""ROM-free Rev C admission, settings, ordered native TCP and teardown tests."""
# SPDX-License-Identifier: GPL-3.0-or-later
import asyncio
import struct

from run_indy500_online import StreamTransport, read
from run_indy_room_options import await_view, launch
from run_online_lobby import Client, board, online, relay, wait_for


async def test(count, latency, fallback):
    g = online.SRALLYC_GAME
    legacy = relay.Room(log=lambda _: None)
    legacy.udp = relay.udp.UdpRelay(legacy.latency)
    hub = online.Hub(legacy, log=lambda _: None, latency_ms=latency, jitter_ms=latency//4, tick_rate=20)

    async def handler(r, w):
        if fallback:
            w._transport = StreamTransport(w.transport)
        await hub.handle(r, w)

    server = await asyncio.start_server(handler, '127.0.0.1', 0)
    port = server.sockets[0].getsockname()[1]
    hub.start()
    clients, sockets = [], []

    async def lobby(name, greeting=online.SRALLYC_HELLO, mask=0, enabled=511):
        c = await Client().connect(port, name=name, mask=mask, enabled=enabled, hello=greeting)
        clients.append(c)
        return c

    async def channel(t, n, **kw):
        r, w = await board(port, g, t, n, **kw)
        sockets.append(w)
        return r, w

    async def reject(t, n=0, **kw):
        r, w = await channel(t, n, **kw)
        assert (await read(r))[0] == b'HEL1'
        assert (await read(r))[0] == b'RLE1'
        w.close()

    try:
        master = await lobby('Car 1', online.BOTH_MODEL2_HELLO)
        old = await lobby('Old Indy', online.INDY500_ROOM_HELLO, mask=511)
        no_rom = await lobby('Exact ROM missing', b'Supermodel Online', mask=511)
        no_settings = await lobby('No native settings capability', enabled=1)
        assert master.hello == online.BOTH_MODEL2_RELAY_HELLO
        assert old.hello == online.INDY500_ROOM_RELAY_HELLO
        assert no_rom.hello == no_settings.hello == b'Supermodel Online lobby v1'
        title = online.fixed('Rev C test', 64)
        for c in (old, no_rom, no_settings):
            c.send(b'OLC1', online.CREATE.pack(g, title))
            assert b'ROM' in await c.until(b'OLE1')
        for kind, mode in ((b'OLC2', 1), (b'OLC3', 2), (b'OLC4', 3)):
            master.send(kind, online.CREATE2.pack(g, title, mode, 1971))
            assert b'central TCP relay' in await master.until(b'OLE1')
        room = await master.create(g)
        for c in (old, no_rom, no_settings):
            c.send(b'OLQ1')
            assert await c.snapshot() == []
            c.send(b'OLJ1', struct.pack('<I', room))
            assert b'ROM' in await c.until(b'OLE1')
        indy = await old.create(online.INDY500_GAME)
        master.send(b'OLQ1')
        assert {r[0] for r in await master.snapshot()} == {room, indy}
        seats = [master]
        await await_view(master, room, (online.SRALLYC_DEFAULT_OPTIONS, 1))
        master.send(b'OLT1', struct.pack('<I', room))
        assert b'2-4' in await master.until(b'OLE1')
        for i in range(1, count):
            c = await lobby(f'Car {i+1}')
            assert c.hello == online.SRALLYC_RELAY_HELLO
            await c.join(room)
            seats.append(c)
        if count == 4:
            extra = await lobby('Fifth car')
            extra.send(b'OLJ1', struct.pack('<I', room))
            assert b'full' in await extra.until(b'OLE1') and not extra.room
        rules = 2 | (3 << 2) | (2 << 5)
        for invalid in (0, 1, 2 | 4 << 2, 2 | 3 << 5, rules|256, rules|512, rules|2048):
            master.send(b'ORO1', struct.pack('<II', room, invalid))
            assert b'valid room options' in await master.until(b'OLE1')
        seats[-1].send(b'ORO1', struct.pack('<II', room, rules))
        assert b'Only the master' in await seats[-1].until(b'OLE1')
        master.send(b'OCN1', struct.pack('<II', room, 1))
        assert b'CAR NUMBER' in await master.until(b'OLE1')
        master.send(b'ORO1', struct.pack('<II', room, rules))
        await asyncio.gather(*(await_view(c, room, (rules, 1)) for c in seats))
        seats[-1].send(b'OLT1', struct.pack('<I', room))
        assert b'Only' in await seats[-1].until(b'OLE1')
        master.send(b'OLT1', struct.pack('<I', room))
        tokens = []
        for i, c in enumerate(seats):
            data = await launch(c, b'OLG5')
            ident, game, index, size, ticket = online.LAUNCH.unpack_from(data)
            assert (ident, game, index, size) == (room, g, i, count)
            assert struct.unpack_from('<II', data, online.LAUNCH.size) == (rules, 0)
            tokens.append(ticket)
        assert len(set(tokens)) == count and bytes(16) not in tokens
        master.send(b'ORO1', struct.pack('<II', room, online.SRALLYC_DEFAULT_OPTIONS))
        assert b'before launch' in await master.until(b'OLE1')
        assert hub.rooms[room].options == rules
        for c in seats:
            c.close()

        # Tickets are game-specific, two-channel and one-session only. Board
        # connection arrival order must not affect either RLA1 index or FE order.
        await reject(bytes(16))
        await reject(tokens[0], bad_guid=relay.INDY500_GUID)
        await reject(tokens[0], registration=b'RLJ3')
        links = {(i,n): await channel(tokens[i],n) for i in reversed(range(count)) for n in (1,0)}
        for (i,n),(r,_) in links.items():
            assert (await read(r))[0] == b'HEL1'
            kind,payload,_ = await read(r)
            assert kind == b'RLA1' and relay.READY.unpack(payload) == (1,i,count,n)
        booked = hub.rooms[room].board
        assert not booked.udp_sessions and not booked.star_active and booked.adaptive_ctl is None
        assert all((c.proto is None) == fallback for channels in booked.players.values() for c in channels.values())
        await reject(tokens[0])
        assert not legacy.players and not legacy.started

        async def ring(src):
            dest = (src-1)%count
            r,w = links[dest,1][0],links[src,0][1]
            frames = [relay.frame(b'NBD1', bytes((tag,i)) + bytes((src,))*3583)
                      for i,tag in enumerate((0xff,0xfe,src+1,0xfc,src+1,0xfc))]
            w.write(frames[0][:3]); await w.drain(); await asyncio.sleep(.02)
            w.write(frames[0][3:] + b''.join(frames[1:4])); await w.drain()
            for frame in frames[:4]:
                assert (await read(r))[2] == frame
            w.write(b''.join(frames[4:])); await w.drain()
            for frame in frames[4:]:
                assert (await read(r))[2] == frame

        await asyncio.gather(*(ring(i) for i in range(count)))
        links[0,0][1].write(relay.frame(b'NBD1', bytes(3585+48 if fallback else 2048)))
        for key,(r,_) in links.items():
            if key != (0,0):
                assert (await read(r))[0] == b'RLE1'
            assert await asyncio.wait_for(r.read(), 3) in (b'', relay.frame(b'RLE1',struct.pack('<II',5,0)))
        await wait_for(lambda: hub.rooms[room].state == 3)
        path = 'stream fallback' if fallback else 'jitter buffer' if latency else 'fast forwarding'
        print(f'PASS Rev C {count} seats, {path}: discovery, frozen options/roster, reverse FE order, exact frames, loss closure')
    finally:
        for c in clients:
            c.close()
        for w in sockets:
            w.close()
        server.close()
        await hub.close()
        await legacy.close()
        await server.wait_closed()


async def main():
    assert online.GAMES[10] == ('srallyc', 0x5bf177da3487f)
    assert online.max_players_for_game(10) == relay.GAME_MAX_PLAYERS['srallyc'] == 4
    assert relay.nbd1_wire_sizes(relay.SRALLYC_GUID) == {3593}
    valid = {2 | d<<2 | m<<5 for d in range(4) for m in range(3)}
    valid |= {v | relay.VIDEO60_OPTION for v in valid}
    assert {v for v in range(1<<14) if online.valid_room_options(10,v)} == valid
    for count in (2,3,4):
        for latency,fallback in ((0,False),(35,False),(35,True)):
            await test(count,latency,fallback)


if __name__ == '__main__':
    asyncio.run(main())
