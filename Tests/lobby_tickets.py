#!/usr/bin/env python3
"""Create and start an online lobby room on a running relay, as the Online tab
would, and print one launch ticket per member (for `supermodel -lobby-ticket=`).

    python3 Tests/lobby_tickets.py --relay 127.0.0.1:1970 --game srally2 --players 2

Output (one line per cabinet, in cabinet order): "<index> <32-hex ticket>".
Standard library only."""
import argparse
import asyncio
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import relay as relay  # noqa: E402
online = relay.lobby

GUI_FLAGS = 1 | 510  # exactly what LobbyClient::Worker sends: AsyncNet opt-in + capabilities (adaptive tick bit 8, star fan-out bit 64, ping bit 128, Video60 bit 256; --no-star clears 64)


class Client:
    async def connect(self, host, port, name, mask, flags=GUI_FLAGS):
        self.reader, self.writer = await asyncio.open_connection(host, port)
        self.writer.write(relay.frame(b'HEL1', b'Supermodel Online') +
                          relay.frame(b'OEH6', online.HELLO.pack(1, mask, flags, online.fixed(name, 32))))
        kind, _, _ = await self.next()
        assert kind == b'HEL1', kind
        await self.snapshot()
        return self

    def send(self, kind, data=b''):
        self.writer.write(relay.frame(kind, data))

    async def next(self):
        return await asyncio.wait_for(relay.read_frame(self.reader), 5)

    async def until(self, wanted):
        for _ in range(200):
            kind, data, _ = await self.next()
            if kind == b'OLE1':
                raise SystemExit('lobby error: ' + data.decode('ascii', 'replace'))
            if kind == wanted or (wanted == b'OLG1' and kind[:3] == b'OLG'):
                return data
        raise AssertionError(wanted)

    async def snapshot(self):
        count, self.ident, self.room = struct.unpack('<III', await self.until(b'OLB1'))
        for _ in range(count):
            await self.until(b'OLR1')
        await self.until(b'OLZ1')

    async def create(self, game, title):
        self.send(b'OLC1', online.CREATE.pack(game, online.fixed(title, 64)))
        while not self.room:
            await self.snapshot()
        return self.room

    async def join(self, room):
        self.send(b'OLJ1', struct.pack('<I', room))
        while self.room != room:
            await self.snapshot()


async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--relay', default='127.0.0.1:1970')
    ap.add_argument('--game', default='srally2', choices=[g for g, _ in online.GAMES])
    ap.add_argument('--players', type=int, default=2)
    ap.add_argument('--title', default='ROM test')
    ap.add_argument('--no-star', action='store_true', help='advertise GUIs without star fan-out so the relay keeps the ring')
    ap.add_argument('--options', type=int, default=None, help='room options mask the master sets before Start Link (1024 = 60 Hz video); pass the same value as -lobby-options= to each cabinet')
    args = ap.parse_args()
    host, port = args.relay.rsplit(':', 1)
    game = [g for g, _ in online.GAMES].index(args.game)
    mask = (1 << len(online.GAMES)) - 1
    flags = GUI_FLAGS & ~64 if args.no_star else GUI_FLAGS
    players = [await Client().connect(host, int(port), f'Cab{i+1}', mask, flags) for i in range(args.players)]
    room = await players[0].create(game, args.title)
    for p in players[1:]:
        await p.join(room)
    if args.options is not None:
        players[0].send(b'ORO1', struct.pack('<II', room, args.options))
        for p in players:
            while struct.unpack('<II', await p.until(b'ORO1'))[1] != args.options:
                pass
    players[0].send(b'OLT1', struct.pack('<I', room))
    launches = [online.LAUNCH.unpack_from(await p.until(b'OLG1')) for p in players]  # OLG1/3/5 share the LAUNCH prefix
    for p in players:
        p.writer.close()
    for room_id, game_id, index, count, ticket in sorted(launches, key=lambda l: l[2]):
        assert room_id == room and game_id == game and count == args.players
        print(index, ticket.hex())


if __name__ == '__main__':
    asyncio.run(main())
