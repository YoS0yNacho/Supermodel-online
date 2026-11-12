#!/usr/bin/env python3
"""ROM-free GUI lobby protocol/authorization/isolation tests. Standard library only."""
import asyncio
import contextlib
import struct
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import relay as relay
online = relay.lobby


class Client:
    async def connect(self, port, name='Player', mask=127, enabled=1):
        self.reader, self.writer = await asyncio.open_connection('127.0.0.1', port)
        self.writer.write(relay.frame(b'HEL1', b'test GUI') + relay.frame(b'OEH6', online.HELLO.pack(1, mask, enabled, online.fixed(name, 32))))
        assert (await self.next())[0] == b'HEL1'
        await self.snapshot()
        return self

    def send(self, kind, data=b''):
        self.writer.write(relay.frame(kind, data))

    async def next(self):
        return await asyncio.wait_for(relay.read_frame(self.reader), 3)

    async def until(self, wanted):
        for _ in range(100):
            kind, data, _ = await self.next()
            if kind == wanted:
                return data
        raise AssertionError(wanted)

    async def snapshot(self):
        count, self.ident, self.room = struct.unpack('<III', await self.until(b'OLB1'))
        rooms = []
        for _ in range(count):
            data = await self.until(b'OLR1')
            rooms.append(online.ROOM.unpack(data[:online.ROOM.size]))
        await self.until(b'OLZ1')  # current OEH6 adds bounded host metadata after room rows
        return rooms

    async def create(self, game=3):
        self.send(b'OLC1', online.CREATE.pack(game, online.fixed('Test lobby', 64)))
        while not self.room:
            await self.snapshot()
        return self.room

    async def join(self, room):
        self.send(b'OLJ1', struct.pack('<I', room))
        while self.room != room:
            await self.snapshot()

    def close(self):
        self.writer.close()


async def board(port, game, ticket, channel, bad_guid=None):
    r, w = await asyncio.open_connection('127.0.0.1', port)
    guid = online.GAMES[game][1] if bad_guid is None else bad_guid
    w.write(relay.frame(b'HEL1', b'native-test') + relay.frame(b'RLJ2', online.JOIN2.pack(1, channel, bytes([channel+1])*16, guid, ticket)))
    return r, w


async def wait_for(predicate):
    async def loop():
        while not predicate():
            await asyncio.sleep(.01)
    await asyncio.wait_for(loop(), 4)


async def main():
    legacy = relay.Room(log=lambda _: None)
    hub = online.Hub(legacy, log=lambda _: None, launch_timeout=.6)
    server = await asyncio.start_server(hub.handle, '127.0.0.1', 0)
    port = server.sockets[0].getsockname()[1]
    hub.start()
    clients = []
    async def client(**kw):
        c = await Client().connect(port, **kw); clients.append(c); return c
    try:
        off = await client(enabled=0)
        off.send(b'OLC1', online.CREATE.pack(3, bytes(64)))
        assert b'AsyncNet' in await off.until(b'OLE1')
        missing = await client(mask=1)
        missing.send(b'OLC1', online.CREATE.pack(3, bytes(64)))
        assert b'ROM' in await missing.until(b'OLE1')
        print('PASS AsyncNet=false rejected; unavailable game rejected')
        off.close(); missing.close()

        for game in range(len(online.GAMES)):
            master = await client(name='Master')
            room_id = await master.create(game)
            master.send(b'OLT1', struct.pack('<I', room_id))
            expected_range = f'2-{online.max_players_for_game(game)}'.encode()
            assert expected_range in await master.until(b'OLE1')
            slave = await client(name='Slave')
            await slave.join(room_id)
            slave.send(b'OLT1', struct.pack('<I', room_id))
            assert b'Only' in await slave.until(b'OLE1')
            master.send(b'OLT1', struct.pack('<I', room_id))
            a = online.LAUNCH.unpack(await master.until(b'OLG1'))
            b = online.LAUNCH.unpack(await slave.until(b'OLG1'))
            assert a[:4] == (room_id, game, 0, 2) and b[:4] == (room_id, game, 1, 2)
            assert a[4] != b[4] and a[4] != bytes(16)
            # Reject wrong exact game and invented capability; don't poison valid roster.
            for ticket, guid in [(bytes([9])*16, None), (a[4], online.GAMES[(game+1)%len(online.GAMES)][1])]:
                r, w = await board(port, game, ticket, 0, guid)
                assert (await relay.read_frame(r))[0] == b'HEL1'
                assert (await relay.read_frame(r))[0] == b'RLE1';w.close()
            # Deliberately reverse arrival order: creator must still be native MASTER.
            channels = {}
            for index, launch in [(1, b), (0, a)]:
                for ch in (1, 0):
                    channels[index, ch] = await board(port, game, launch[4], ch)
            for (index, ch), (r, w) in channels.items():
                assert (await asyncio.wait_for(relay.read_frame(r), 3))[0] == b'HEL1'
                kind, data, _ = await relay.read_frame(r)
                assert kind == b'RLA1' and relay.READY.unpack(data) == (1, index, 2, ch)
            payload = ((bytes(range(256)) * 13)[:3264] if game == 5 else
                       bytes(range(256)) * 8 if game in (4,6) else
                       bytes(range(256)) * 4 if game >= 2 else
                       bytes([73]) * 848)
            channels[0, 0][1].write(relay.frame(b'NBD1', payload))
            assert (await relay.read_frame(channels[1, 1][0]))[1] == payload
            # No live late joins or ticket reuse after start.
            r, w = await board(port, game, a[4], 0)
            assert (await relay.read_frame(r))[0] == b'HEL1'
            assert (await relay.read_frame(r))[0] == b'RLE1';w.close()
            assert not legacy.players and not legacy.started
            master.close();slave.close()
            for r, w in channels.values(): w.close()
            await wait_for(lambda: hub.rooms[room_id].board.failed)
            await hub.destroy(hub.rooms[room_id], 'test complete')
            print(f'PASS {online.GAMES[game][0]} master authorization, frozen role order, capability/game rejection, raw forwarding, disconnect, legacy isolation')

        # srally2 supports the role encodings for at most four cabinets.
        rally = [await client(name=f'Rally{i}') for i in range(5)]
        rally_room = await rally[0].create(4)
        for c in rally[1:4]:
            await c.join(rally_room)
        rally[4].send(b'OLJ1', struct.pack('<I', rally_room))
        assert b'full' in await rally[4].until(b'OLE1')
        for c in rally: c.close()
        await hub.destroy(hub.rooms[rally_room], 'test complete')
        print('PASS srally2 four-cabinet waiting-room limit')

        # lemans24 supports at most six linked cabinets (otherCount 0..5).
        lm = [await client(name=f'LeMans{i}') for i in range(7)]
        lm_room = await lm[0].create(5)
        for c in lm[1:6]:
            await c.join(lm_room)
        lm[6].send(b'OLJ1', struct.pack('<I', lm_room))
        assert b'full' in await lm[6].until(b'OLE1')
        for c in lm: c.close()
        await hub.destroy(hub.rooms[lm_room], 'test complete')
        print('PASS lemans24 six-cabinet waiting-room limit')

        # Independent lobbies, limits, and waiting-room departures.
        a, b, c = await client(), await client(), await client()
        first = await a.create(0); second = await b.create(3)
        await c.join(second)
        assert hub.rooms[first].members[0]['id'] == a.ident
        assert len(hub.rooms[first].members) == 1 and len(hub.rooms[second].members) == 2
        a.send(b'OLT1', struct.pack('<I', second))
        assert b'Only' in await a.until(b'OLE1')
        c.send(b'OLL1');await wait_for(lambda: len(hub.rooms[second].members) == 1)
        b.close();await wait_for(lambda: second not in hub.rooms)
        a.close();await wait_for(lambda: first not in hub.rooms)
        c.close()
        print('PASS multiple independent rooms, cross-room start denial, leave, host departure')

        # Eight members; ninth rejected; cancelled launch fails closed (no partial ring).
        players = [await client(name=f'P{i}') for i in range(9)]
        room_id = await players[0].create(2)
        for c in players[1:8]: await c.join(room_id)
        players[8].send(b'OLJ1', struct.pack('<I', room_id))
        assert b'full' in await players[8].until(b'OLE1')
        players[0].send(b'OLT1', struct.pack('<I', room_id))
        launches = [online.LAUNCH.unpack(await p.until(b'OLG1')) for p in players[:8]]
        assert [x[2] for x in launches] == list(range(8))
        assert len({x[4] for x in launches}) == 8
        await wait_for(lambda: hub.rooms[room_id].state == 3)
        assert not hub.rooms[room_id].board.started
        await hub.destroy(hub.rooms[room_id], 'timeout test')
        print('PASS 8 reserved slots, full-room rejection, launch timeout without partial start')

        # Wrong protocol/oversized frames are closed, never crash the hub.
        r, w = await asyncio.open_connection('127.0.0.1', port)
        w.write(struct.pack('<I4s', 999999, b'OLH1'))
        assert await asyncio.wait_for(r.read(), 3) == b'';w.close()
        print('PASS malformed-frame bound')

        # Lounge chat, lobby room chat isolation, late-join history, and in-game RLC1 chat continuity.
        c1 = await client(name='Host')
        c2 = await client(name='Racer2')
        c3 = await client(name='OtherRoom')
        c1.send(b'OLS1', online.chat_fixed('Hola desde el lounge!', 160))
        m_lounge = online.CHAT_MSG.unpack(await c2.until(b'OLM1'))
        assert m_lounge[1] == 0 and m_lounge[2] == 0
        assert online.chat_text(m_lounge[3]) == 'Host'
        assert online.chat_text(m_lounge[4]) == 'Hola desde el lounge!'
        await c3.until(b'OLM1')

        chat_room = await c1.create(1)
        other_room = await c3.create(2)
        c1.send(b'OLS1', online.chat_fixed('Bienvenidos al lobby!', 160))
        own_echo = online.CHAT_MSG.unpack(await c1.until(b'OLM1'))
        assert own_echo[1] == chat_room and own_echo[2] == 1 and online.chat_text(own_echo[4]) == 'Bienvenidos al lobby!'
        # Late joiner receives room chat history after OLZ1, while other_room receives nothing.
        await c2.join(chat_room)
        replayed = online.CHAT_MSG.unpack(await c2.until(b'OLM1'))
        assert replayed[0] == own_echo[0] and replayed[1] == chat_room and replayed[2] == 1
        assert online.chat_text(replayed[4]) == 'Bienvenidos al lobby!'

        # Reject RLC1 before Start Link or with bogus ticket.
        bad_r, bad_w = await asyncio.open_connection('127.0.0.1', port)
        bad_w.write(relay.frame(b'HEL1', b'chat-test') + relay.frame(b'RLC1', online.CHAT_JOIN.pack(1, bytes([7]) * 16, online.chat_fixed('Fake', 32))))
        assert (await relay.read_frame(bad_r))[0] == b'HEL1'
        assert (await relay.read_frame(bad_r))[0] == b'RLE1'
        bad_w.close()

        # Authorize Start Link and connect in-game RLC1 chat for both cabinets.
        c1.send(b'OLT1', struct.pack('<I', chat_room))
        la = online.LAUNCH.unpack(await c1.until(b'OLG1'))
        lb = online.LAUNCH.unpack(await c2.until(b'OLG1'))
        c1.close(); c2.close()

        ig1_r, ig1_w = await asyncio.open_connection('127.0.0.1', port)
        ig2_r, ig2_w = await asyncio.open_connection('127.0.0.1', port)
        ig1_w.write(relay.frame(b'HEL1', b'chat') + relay.frame(b'RLC1', online.CHAT_JOIN.pack(1, la[4], online.chat_fixed('Host', 32))))
        ig2_w.write(relay.frame(b'HEL1', b'chat') + relay.frame(b'RLC1', online.CHAT_JOIN.pack(1, lb[4], online.chat_fixed('Racer2', 32))))
        assert (await relay.read_frame(ig1_r))[0] == b'HEL1'
        k1, d1, _ = await relay.read_frame(ig1_r)
        assert k1 == b'RLC2' and online.CHAT_WELCOME.unpack(d1)[:3] == (chat_room, 1, 2)
        # Lobby chat message carries seamlessly into in-game chat on connect!
        k_hist, d_hist, _ = await relay.read_frame(ig1_r)
        assert k_hist == b'OLM1' and online.chat_text(online.CHAT_MSG.unpack(d_hist)[4]) == 'Bienvenidos al lobby!'

        assert (await relay.read_frame(ig2_r))[0] == b'HEL1'
        k2, d2, _ = await relay.read_frame(ig2_r)
        assert k2 == b'RLC2' and online.CHAT_WELCOME.unpack(d2)[:3] == (chat_room, 2, 2)
        await relay.read_frame(ig2_r)  # replayed lobby message

        # Bidirectional UTF-8 in-game chat + keepalive ping/pong.
        ig2_w.write(relay.frame(b'OLS1', online.chat_fixed('¡Buena carrera! ¿Otra vuelta?', 160)))
        for reader in (ig1_r, ig2_r):
            mk, md, _ = await relay.read_frame(reader)
            parsed = online.CHAT_MSG.unpack(md)
            assert mk == b'OLM1' and parsed[1] == chat_room and parsed[2] == 2
            assert online.chat_text(parsed[3]) == 'Racer2'
            assert online.chat_text(parsed[4]) == '¡Buena carrera! ¿Otra vuelta?'
        ig1_w.write(relay.frame(b'OLP1'))
        assert (await relay.read_frame(ig1_r))[0] == b'OLK1'

        # Verify c3 in other_room never received chat_room's messages.
        c3.send(b'OLQ1')
        await c3.snapshot()
        ig1_w.close(); ig2_w.close(); c3.close()
        await hub.destroy(hub.rooms[chat_room], 'chat test complete')
        await hub.destroy(hub.rooms[other_room], 'chat test complete')
        print('PASS lounge chat, lobby chat, late-join replay, room isolation, and RLC1 in-game UTF-8 chat; all Online lobby tests passed')
    finally:
        for c in clients: c.close()
        server.close();await server.wait_closed();await hub.close();await legacy.close()

if __name__ == '__main__':
    asyncio.run(main())
