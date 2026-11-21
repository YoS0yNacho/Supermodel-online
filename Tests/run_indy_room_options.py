#!/usr/bin/env python3
"""ROM-free INDY 500 lobby settings, frozen launch, and compatibility checks."""
import asyncio
import struct

from run_online_lobby import Client, online, relay


async def room_view(client, room_id):
    """Find an options row and shared-capability row in the same snapshot."""
    await client.until(b'OLB1')
    options = shared = None
    while True:
        kind, body, _ = await client.next()
        if kind == b'OLZ1':
            return options, shared
        if kind in (b'ORO1', b'ORN1') and struct.unpack_from('<I', body)[0] == room_id:
            if kind == b'ORO1':
                _, options = struct.unpack('<II', body)
            else:
                _, shared, n = struct.unpack_from('<III', body)
                assert len(body) == 12 + n * 8
                assert all(struct.unpack_from('<II', body, 12 + i * 8)[1] == 0 for i in range(n))


async def await_view(client, room_id, expected):
    for _ in range(25):
        if await room_view(client, room_id) == expected:
            return
    raise AssertionError((room_id, expected))


async def launch(client, kind):
    for _ in range(100):
        k, data, _ = await client.next()
        if k == b'OLE1':
            raise AssertionError(data)
        if k.startswith(b'OLG'):
            assert k == kind, (k, kind)
            return data
    raise AssertionError(kind)


async def main():
    game = online.INDY500_GAME
    rules = 2 | (4 << 2) | (1 << 5)  # Hardest / Long / Handicap Off
    default = 2 | (2 << 2) | (1 << 11)
    valid = {0, *(2 | d << 2 | m << 5 | h << 11 for d in range(5) for m in range(2) for h in range(2))}
    valid |= {v | relay.VIDEO60_OPTION for v in valid}
    assert {v for v in range(1 << 14) if online.valid_room_options(game, v)} == valid
    assert not online.valid_room_options(3, rules | (1 << 11))
    legacy = relay.Room(log=lambda _: None)
    hub = online.Hub(legacy, log=lambda _: None)
    server = await asyncio.start_server(hub.handle, '127.0.0.1', 0)
    port = server.sockets[0].getsockname()[1]
    hub.start()
    clients = []

    async def connect(name, room_settings):
        # Even the old INDY clients already advertise the Model 3 native bit.
        hello = online.INDY500_ROOM_HELLO if room_settings else online.INDY500_HELLO
        c = await Client().connect(port, name=name, enabled=511, hello=hello)
        assert c.hello == (online.INDY500_ROOM_RELAY_HELLO if room_settings else online.RELAY_HELLO)
        clients.append(c)
        return c

    try:
        master = await connect('Master', True)
        guest = await connect('Guest', True)
        old = await connect('Old Indy', False)
        mixed = await master.create(game)
        master.send(b'OLQ1')
        await await_view(master, mixed, (default, 1))
        await old.join(mixed)
        await await_view(master, mixed, (0, 0))
        old.send(b'OLQ1')
        assert await room_view(old, mixed) == (None, None)
        master.send(b'ORO1', struct.pack('<II', mixed, rules))
        assert b'update all clients' in await master.until(b'OLE1')
        master.send(b'OLT1', struct.pack('<I', mixed))
        a = await launch(master, b'OLG5')
        b = await launch(old, b'OLG1')
        assert struct.unpack_from('<II', a, online.LAUNCH.size) == (0, 0)
        assert len(b) == online.LAUNCH.size
        assert online.LAUNCH.unpack(b)[:4] == (mixed, game, 1, 2)
        master.close(); old.close()
        await hub.destroy(hub.rooms[mixed], 'mixed test complete')
        print('PASS Indy mixed-client default launch: no override, no extra option frames for old clients')

        master = await connect('Updated Master', True)
        old = await connect('Legacy spectator', False)
        room = await master.create(game)
        await guest.join(room)
        master.send(b'OLQ1')
        await await_view(master, room, (default, 1))
        # Known Model 3 flags, invalid fields, and non-master changes must fail.
        for invalid in (1, 1 << 11, rules | 256, rules | 512, 2 | 5 << 2, 2 | 2 << 5, 1 << 31):
            master.send(b'ORO1', struct.pack('<II', room, invalid))
            assert b'valid room options' in await master.until(b'OLE1')
        guest.send(b'ORO1', struct.pack('<II', room, rules))
        assert b'Only the master' in await guest.until(b'OLE1')
        master.send(b'ORO1', struct.pack('<II', room, 0))  # the previous GUI can still opt out
        await asyncio.gather(*(await_view(c, room, (0, 1)) for c in (master, guest)))
        master.send(b'ORO1', struct.pack('<II', room, default))
        await asyncio.gather(*(await_view(c, room, (default, 1)) for c in (master, guest)))
        master.send(b'ORO1', struct.pack('<II', room, rules))
        await asyncio.gather(*(await_view(c, room, (rules, 1)) for c in (master, guest)))
        old.send(b'OLJ1', struct.pack('<I', room))
        assert b'INDY 500 room settings' in await old.until(b'OLE1')
        master.send(b'OLT1', struct.pack('<I', room))
        for i, c in enumerate((master, guest)):
            data = await launch(c, b'OLG5')
            assert online.LAUNCH.unpack_from(data)[:4] == (room, game, i, 2)
            assert struct.unpack_from('<II', data, online.LAUNCH.size) == (rules, 0)
        master.send(b'ORO1', struct.pack('<II', room, default))
        assert b'before launch' in await master.until(b'OLE1')
        assert hub.rooms[room].options == rules
        print('PASS Indy options: validated master-only edits, broadcast/reset, legacy rejection, frozen options on every launch')
    finally:
        for c in clients:
            c.close()
        server.close()
        await hub.close()
        await legacy.close()
        await server.wait_closed()


if __name__ == '__main__':
    asyncio.run(main())
