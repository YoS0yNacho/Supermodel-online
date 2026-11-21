#!/usr/bin/env python3
"""ROM-free Model 2 room video negotiation with both old and current lobby hellos."""
import asyncio
import struct

from run_indy_room_options import await_view, launch
from run_online_lobby import Client, online, relay, wait_for


async def main():
    video = relay.VIDEO60_OPTION
    # Exercise each ROM-availability bit; an upgraded multi-title guest can
    # join any of the three. The previous client has *all* OEH6 bits (even
    # Model 3 Video60), but only the previous game-availability hello.
    games = ((online.INDY500_GAME, 1, online.INDY500_DEFAULT_OPTIONS, 2 | 4 << 2 | 1 << 5),
             (online.SRALLYC_GAME, 2, online.SRALLYC_DEFAULT_OPTIONS, 2 | 3 << 2 | 2 << 5),
             (online.DAYTONA_GAME, 4, online.DAYTONA_DEFAULT_OPTIONS, 2 | 3 << 2 | 1 << 5))
    legacy = relay.Room(log=lambda _: None)
    hub = online.Hub(legacy, log=lambda _: None)
    server = await asyncio.start_server(hub.handle, '127.0.0.1', 0)
    port = server.sockets[0].getsockname()[1]
    hub.start()
    clients = []

    async def connect(name, mask, bits=511):
        if mask is None:
            hello, reply = online.DAYTONA_HELLO + b'3', online.DAYTONA_RELAY_HELLO + b'3'
        else:
            suffix = str(mask).encode('ascii')
            hello, reply = online.MODEL2_VIDEO_HELLO + suffix, online.MODEL2_VIDEO_RELAY_HELLO + suffix
        c = await Client().connect(port, name=name, enabled=bits, hello=hello)
        clients.append(c)
        if bits & 6 == 6:
            assert c.hello == reply
            admitted = hub.clients[c.ident]
            assert admitted['video_capable'] and admitted['model2_video_capable'] == (mask is not None)
            wanted = 7 if mask is None else mask
            assert admitted['mask'] >> online.INDY500_GAME == wanted
        else:
            assert c.hello != reply
            assert not hub.clients[c.ident]['model2_video_capable']
        return c

    def options(c, room, value):
        c.send(b'ORO1', struct.pack('<II', room, value))

    async def start(room, game, expected, *seats):
        seats[0].send(b'OLT1', struct.pack('<I', room))
        tickets = set()
        for i, c in enumerate(seats):
            data = await launch(c, b'OLG5')
            assert len(data) == online.LAUNCH.size + 8
            ident, rom, index, count, ticket = online.LAUNCH.unpack_from(data)
            assert (ident, rom, index, count) == (room, game, i, len(seats))
            assert struct.unpack_from('<II', data, online.LAUNCH.size) == (expected, 0)
            assert ticket != bytes(16) and ticket not in tickets
            tickets.add(ticket)
        options(seats[0], room, expected ^ video)
        assert b'before launch' in await seats[0].until(b'OLE1')
        assert hub.rooms[room].state == 1 and hub.rooms[room].options == expected
        assert not hub.rooms[room].board.star and not hub.rooms[room].board.adaptive
        await hub.destroy(hub.rooms[room], 'video test completed')

    try:
        incomplete = await connect('No native options', 7, 1 | 256)
        incomplete.close()
        for game, mask, default, custom in games:
            master = await connect('Updated master', mask)
            old = await connect('Previous Model 2', None)
            peer = await connect('Updated guest', 7)
            room = await master.create(game)
            master.send(b'OLQ1')
            await await_view(master, room, (default, 1))
            await old.join(room)
            options(master, room, custom | video)
            assert b'60 Hz' in await master.until(b'OLE1')
            assert hub.rooms[room].effective_options() == default
            old.send(b'OLL1')
            await wait_for(lambda: len(hub.rooms[room].members) == 1)
            await peer.join(room)
            options(peer, room, custom | video)
            assert b'Only the master' in await peer.until(b'OLE1')
            options(master, room, custom | video)
            await asyncio.gather(*(await_view(c, room, (custom | video, 1)) for c in (master, peer)))
            old.send(b'OLQ1')
            await await_view(old, room, (custom, 1))
            old.send(b'OLJ1', struct.pack('<I', room))
            assert b'60 Hz' in await old.until(b'OLE1')
            assert hub.clients[old.ident]['room'] == 0
            options(master, room, custom)
            await await_view(peer, room, (custom, 1))
            await old.join(room)
            await wait_for(lambda: len(hub.rooms[room].members) == 3)
            await start(room, game, custom, master, peer, old)
            for c in (master, peer, old):
                c.close()

            master = await connect('60 Hz master', 7)
            peer = await connect('60 Hz guest', mask)
            room = await master.create(game)
            await peer.join(room)
            # Indy can also change just video without forcing shared EEPROM rules.
            chosen = video if game == online.INDY500_GAME else custom | video
            options(master, room, chosen)
            await asyncio.gather(*(await_view(c, room, (chosen, 1)) for c in (master, peer)))
            await start(room, game, chosen, master, peer)
            master.close()
            peer.close()
            print(f'PASS {online.GAMES[game][0]}: distinct 60 Hz hello, mixed/native rollback, master-only, masked listings, join gating, frozen launch')
    finally:
        for c in clients:
            c.close()
        server.close()
        await server.wait_closed()
        await hub.close()
        await legacy.close()


if __name__ == '__main__':
    asyncio.run(main())
