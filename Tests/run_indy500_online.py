#!/usr/bin/env python3
"""ROM-free INDY 500 lobby and ordered native TCP ring checks (GPL-3.0-or-later)."""
import asyncio
import struct

from run_online_lobby import Client, board, online, relay, wait_for


class StreamTransport:
    """Keep the StreamReader protocol installed to exercise Room.handle's fallback."""
    def __init__(self, transport):
        self.transport = transport

    def __getattr__(self, name):
        if name in ('get_protocol', 'set_protocol'):
            raise AttributeError(name)
        return getattr(self.transport, name)


async def read(reader):
    return await asyncio.wait_for(relay.read_frame(reader), 3)


async def test(players, fallback, latency, jitter):
    game = online.INDY500_GAME
    legacy = relay.Room(log=lambda _: None)
    legacy.udp = relay.udp.UdpRelay(legacy.latency)
    hub = online.Hub(legacy, log=lambda _: None, latency_ms=latency, jitter_ms=jitter, tick_rate=20)
    clients, sockets = [], []

    async def handler(r, w):
        if fallback:
            w._transport = StreamTransport(w.transport)
        await hub.handle(r, w)

    server = await asyncio.start_server(handler, '127.0.0.1', 0)
    port = server.sockets[0].getsockname()[1]
    hub.start()

    async def lobby(name, indy=True):
        c = await Client().connect(port, name=name, enabled=511, hello=online.INDY500_HELLO if indy else b'Supermodel Online')
        clients.append(c)
        return c

    async def channel(ticket, number, **kwargs):
        r, w = await board(port, game, ticket, number, **kwargs)
        sockets.append(w)
        return r, w

    async def reject(ticket, **kwargs):
        r, w = await channel(ticket, 0, **kwargs)
        assert (await read(r))[0] == b'HEL1'
        assert (await read(r))[0] == b'RLE1'
        w.close()

    try:
        master = await lobby('Master')
        legacy_client = await lobby('Model 3 only', indy=False)
        title = online.fixed('INDY 500 lobby prototype test', 64)
        for kind, mode in ((b'OLC2', 1), (b'OLC3', 2), (b'OLC4', 3)):
            master.send(kind, online.CREATE2.pack(game, title, mode, 1971))
            assert b'central TCP' in await master.until(b'OLE1')
        room = await master.create(game)
        # Legacy HELLO and ROM masks must continue to discover and create the
        # original games, without receiving an unknown game ID in OLR1.
        assert await legacy_client.snapshot() == []
        legacy_client.send(b'OLJ1', struct.pack('<I', room))
        assert b'ROM' in await legacy_client.until(b'OLE1')
        model3 = await legacy_client.create(3)
        assert {r[0] for r in await master.snapshot()} == {room, model3}
        master.send(b'ORO1', struct.pack('<II', room, 1024))
        assert b'valid room options' in await master.until(b'OLE1')
        master.send(b'OLT1', struct.pack('<I', room))
        assert b'2-8' in await master.until(b'OLE1')
        seats = [master]
        for i in range(1, players):
            seats.append(await lobby(f'Slave {i+1}'))
            await seats[-1].join(room)
        if players == 8:
            extra = await lobby('No ninth seat')
            extra.send(b'OLJ1', struct.pack('<I', room))
            assert b'full' in await extra.until(b'OLE1') and not extra.room
        seats[-1].send(b'OLT1', struct.pack('<I', room))
        assert b'Only' in await seats[-1].until(b'OLE1')
        master.send(b'OLT1', struct.pack('<I', room))
        launches = [online.LAUNCH.unpack(await c.until(b'OLG1')) for c in seats]
        for index, launch in enumerate(launches):
            assert launch[:4] == (room, game, index, players)
        tickets = [launch[4] for launch in launches]
        assert len(set(tickets)) == players and bytes(16) not in tickets
        for c in seats:
            c.close()
        await reject(bytes(16))
        await reject(tickets[0], bad_guid=relay.GUID)
        await reject(tickets[0], registration=b'RLJ3')  # not a UDP-capable registration

        # Reversed arrival order does not change the reserved cabinet order.
        links = {(index, ch): await channel(tickets[index], ch)
                 for index in reversed(range(players)) for ch in (1, 0)}
        for (index, ch), (r, _) in links.items():
            assert (await read(r))[0] == b'HEL1'
            kind, payload, _ = await read(r)
            assert kind == b'RLA1' and relay.READY.unpack(payload) == (1, index, players, ch)
        board_room = hub.rooms[room].board
        assert not board_room.udp_sessions and board_room.adaptive_ctl is None and not board_room.star_active
        assert all((c.proto is None) == fallback for channels in board_room.players.values() for c in channels.values())
        await reject(tickets[0])  # no rejoining a running room

        async def ring(src):
            # FE assigns the highest ID first; the native ring runs in order
            # 1 -> N -> ... -> 2 -> 1, without reordering ticket/RLA1 slots.
            dest = (src - 1) % players
            types = (0xff, 0xfe, src+1, 0xfc, 0xfd, src+1, 0xfc)
            w = links[src, 0][1]
            def payload(tag, i):
                if tag == 0xfd:
                    # Pre-intro readiness is opaque, padded NBD1 data on
                    # every seat count, including the maximum-size room.
                    prefix = b'\xfdIEN1\x01\0\0'
                    ring_index = (players-src) % players
                    turn = struct.pack('<10IqQ', 0x31533244, 1, 1, 1, ring_index,
                                       1, 1, 0, 0, (1 << players)-1, 0, 61000)
                    return (prefix + turn).ljust(3585, b'\0')
                return bytes((tag, i)) + bytes((i,)) * 3583
            frames = [relay.frame(b'NBD1', payload(tag, i)) for i, tag in enumerate(types)]
            # Partial header, then coalesced frames; a second batch exercises the
            # optimised path when latency is disabled and no bytes are buffered.
            w.write(frames[0][:5]); await w.drain(); await asyncio.sleep(.03)
            w.write(frames[0][5:] + b''.join(frames[1:3])); await w.drain()
            for original in frames[:3]:
                assert (await read(links[dest, 1][0]))[2] == original
            w.write(b''.join(frames[3:])); await w.drain()
            for original in frames[3:]:
                assert (await read(links[dest, 1][0]))[2] == original

        await asyncio.gather(*(ring(i) for i in range(players)))
        # A valid Model 3 setup (8 bytes) / timed payload is not an Indy frame.
        # Fail the whole session, not just that message, to avoid a desynced ring.
        invalid = 8 if not fallback else 3585 + 48
        links[0, 0][1].write(relay.frame(b'NBD1', bytes(invalid)))
        for key, (r, _) in links.items():
            if key != (0, 0):  # the offending output may already be closed
                assert (await read(r))[0] == b'RLE1'
            assert await asyncio.wait_for(r.read(), 3) in (b'', relay.frame(b'RLE1', struct.pack('<II', 5, 0)))
        await wait_for(lambda: hub.rooms[room].state == 3)
        path = 'coroutine' if fallback else 'buffered/jitter' if latency else 'fast/fragmented'
        print(f'PASS INDY 500 {path}, {players} seats: discovery, locked roster, native ring order, opaque frames, whole-room failure'
              + (', ninth seat rejected' if players == 8 else ''))
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
    assert online.GAMES[9] == ('indy500', 0x5bf177da3487e)
    assert online.max_players_for_game(9) == relay.GAME_MAX_PLAYERS['indy500'] == 8
    assert relay.nbd1_wire_sizes(relay.INDY500_GUID) == {3593}
    assert online.valid_room_options(9, 0) and online.valid_room_options(9, relay.VIDEO60_OPTION)
    for players in range(2, 9):
        await test(players, False, 0, 0)
        await test(players, False, 20, 5)
        await test(players, True, 20, 5)


if __name__ == '__main__':
    asyncio.run(main())
