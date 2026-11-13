#!/usr/bin/env python3
# Standalone eight-player + Scud Plus Secret Cars relay. GPL-3.0-or-later.
# One file, Python3.9+/PyPy3, standard library only. No sibling modules needed.
# Run: pypy3 relay.py --bind 0.0.0.0 --port 1970 --tick-rate 20
# Same protocol/game identities/nonverbose core as the matching client release.

"""GUI-lobby exact dayto2pe / daytona2 / scud / scudplus / srally2 / lemans24 async relay. Python 3.9+, standard library only.

Clients: supermodel -relay=HOST:1970 opens the Online GUI.
The lobby master presses Start Link.
Use a trusted VPN/firewall. See Docs/OnlineLobbies.md.

--latency-ms / --jitter-ms add fake one-way latency to relayed game traffic,
useful for testing prediction and net-board behaviour without real distance.
--tick-rate configures the game update rate (5-60 Hz, default 60) sent to
connected emulators so cabinets pace ring updates instead of spamming 60 Hz.
"""
import argparse
import asyncio
import contextlib
import gc
import random
import socket
import struct
import sys
import time

VERSION = 1
_WINMM_HIGH_RES = False
GUID = 0x5BF177DA34875  # legacy export, kept for existing Daytona launchers/tests
GAMES = {GUID: ("dayto2pe", 848), 0x5BF177DA34876: ("scud", 1024),
         0x5BF177DA34877: ("scudplus", 1024), 0x5BF177DA34878: ("daytona2", 848),
         0x5BF177DA34879: ("srally2", 2048), 0x5BF177DA3487B: ("srally2_8p", 2048), 0x5BF177DA3487A: ("lemans24", 3264)}
GAME_MAX_PLAYERS = {"daytona2": 16, "dayto2pe": 16, "srally2": 4, "srally2_8p": 8, "lemans24": 6}
MAX_PLAYERS = 8
MAX_FRAME_SIZE = 3320
MIN_TICK_RATE = 5
MAX_TICK_RATE = 60
DEFAULT_TICK_RATE = 60
WRITE_HIGH_WATER = 262144
WRITE_LOW_WATER = 65536
STALL_TIMEOUT_SEC = 30.0

HEADER = struct.Struct('<I4s')
HEADER_U32 = struct.Struct('<II')
_UNPACK_U32 = HEADER_U32.unpack_from
JOIN = struct.Struct('<II16sQ')
READY = struct.Struct('<IIII')
TICK_RATE = struct.Struct('<II')

NBD1_ID = int.from_bytes(b'NBD1', 'little')
PIN1_ID = int.from_bytes(b'PIN1', 'little')
PON1_ID = int.from_bytes(b'PON1', 'little')


def _enable_winmm_high_res():
    global _WINMM_HIGH_RES
    if sys.platform == 'win32' and not _WINMM_HIGH_RES:
        with contextlib.suppress(Exception):
            import ctypes
            ctypes.windll.winmm.timeBeginPeriod(1)
            _WINMM_HIGH_RES = True


def tune_socket(writer):
    """Enable low-delay TCP options on a relay transport socket (best-effort)."""
    sock = writer.get_extra_info('socket')
    if sock is None:
        return
    with contextlib.suppress(OSError, AttributeError):
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    with contextlib.suppress(OSError, AttributeError):
        # IPTOS_LOWDELAY (0x10) helps home routers / Wi-Fi prioritize small 60 Hz frames.
        if sock.family == socket.AF_INET:
            sock.setsockopt(socket.IPPROTO_IP, socket.IP_TOS, 0x10)
    for opt in (getattr(socket, 'SO_SNDBUF', None), getattr(socket, 'SO_RCVBUF', None)):
        if opt is not None:
            with contextlib.suppress(OSError):
                if sock.getsockopt(socket.SOL_SOCKET, opt) < WRITE_HIGH_WATER:
                    sock.setsockopt(socket.SOL_SOCKET, opt, WRITE_HIGH_WATER)


def frame(kind, payload=b''):
    return HEADER.pack(8 + len(payload), kind) + payload


async def read_frame(reader):
    header = await reader.readexactly(8)
    size, kind = HEADER.unpack(header)
    if not 8 <= size <= MAX_FRAME_SIZE:
        raise ValueError('invalid frame size')
    body = await reader.readexactly(size - 8)
    return kind, body, header + body


class Latency:
    """Fake one-way latency applied to relayed game traffic (0 ms = off).

    Frames are delayed individually from their arrival time instead of sleeping
    inside the read loop: pausing the read loop would throttle the sender rather
    than emulate distance. Pipelined frames therefore keep their spacing and all
    arrive one latency later, and the round-trip time the cabinets measure is
    roughly twice the configured value (once per direction).
    """

    MAX_BACKLOG = 4096  # frames buffered per direction; overflow fails the room

    def __init__(self, latency_ms=0, jitter_ms=0):
        self.latency_ms = max(0, int(latency_ms))
        self.jitter_ms = max(0, int(jitter_ms))
        self.rng = random.Random()
        if self.latency_ms or self.jitter_ms:
            _enable_winmm_high_res()

    @property
    def enabled(self):
        return bool(self.latency_ms or self.jitter_ms)

    def sample(self):
        """One-way delay in seconds; jitter is uniform and never negative."""
        if not self.jitter_ms:
            return self.latency_ms / 1000
        return max(0, self.latency_ms + self.rng.randint(-self.jitter_ms, self.jitter_ms)) / 1000

    def describe(self):
        text = f'{self.latency_ms} ms one-way per direction'
        if self.jitter_ms:
            text += f', +/-{self.jitter_ms} ms random jitter'
        return text


class FastChannelProtocol(asyncio.Protocol):
    """Zero-coroutine game-frame forwarder friendly to both CPython and PyPy3 JIT.

    Once a game channel finishes its 2-frame RLJ1/RLJ2 handshake, swapping its
    transport protocol to FastChannelProtocol removes StreamReader/StreamWriter
    coroutines, per-packet Task/TimerHandle allocations (`wait_for(drain)`), and
    per-packet `header + body` byte copies from the 60 Hz * N^2 hot path.
    """

    __slots__ = (
        'room', 'transport', 'orig_proto', 'done', 'loop',
        '_buf', 'peer_write', 'inbound_peer', 'backlog_put',
        'fast1', 'fast2', 'valid_nbd1', '_stall_timer',
    )

    def __init__(self, room, transport, orig_proto, loop):
        self.room = room
        self.transport = transport
        self.orig_proto = orig_proto
        self.loop = loop
        self.done = loop.create_future()
        self._buf = bytearray()
        self.peer_write = None
        self.inbound_peer = None
        self.backlog_put = None
        self.fast1 = 0
        self.fast2 = 0
        self.valid_nbd1 = frozenset()
        self._stall_timer = None

    def _fail(self, exc):
        if self._stall_timer is not None:
            self._stall_timer.cancel()
            self._stall_timer = None
        if not self.done.done():
            self.done.set_exception(exc)
        self.transport.close()

    def bind_peer(self, peer_transport, fast1, fast2, valid_nbd1, backlog=None):
        self.peer_write = peer_transport.write
        self.fast1 = fast1
        self.fast2 = fast2
        self.valid_nbd1 = valid_nbd1
        self.backlog_put = backlog.put_nowait if backlog is not None else None

    def data_received(self, data):
        if not data or self.done.done():
            return
        room = self.room
        buf = self._buf
        peer_write = self.peer_write
        active = room.started and peer_write is not None and not room.failed
        # Fast zero-copy path: no partial leftover bytes and fake-latency is off.
        if not buf and self.backlog_put is None:
            n = len(data)
            offset = 0
            fast1 = self.fast1
            fast2 = self.fast2
            valid_nbd1 = self.valid_nbd1
            while n - offset >= 8:
                size, kind_u32 = _UNPACK_U32(data, offset)
                if not 8 <= size <= MAX_FRAME_SIZE:
                    self._fail(ValueError('invalid frame size'))
                    return
                if not active:
                    if offset + size <= n:
                        self._fail(ValueError('traffic before authorized lobby start'))
                        return
                    break
                if not ((kind_u32 == NBD1_ID and (size == fast1 or size == fast2 or size in valid_nbd1))
                        or (kind_u32 == PIN1_ID and size == 16)
                        or (kind_u32 == PON1_ID and size == 24)):
                    self._fail(ValueError('invalid relay data message'))
                    return
                if offset + size > n:
                    break
                offset += size
            if offset == n:
                peer_write(data)
                return
            if offset > 0:
                with memoryview(data) as mv:
                    peer_write(mv[:offset])
                    buf.extend(mv[offset:])
            else:
                buf.extend(data)
            return

        # Buffered / split-segment or fake-latency path.
        buf.extend(data)
        n = len(buf)
        offset = 0
        fast1 = self.fast1
        fast2 = self.fast2
        valid_nbd1 = self.valid_nbd1
        backlog_put = self.backlog_put
        now = time.monotonic() if backlog_put is not None else 0.0
        while n - offset >= 8:
            size, kind_u32 = _UNPACK_U32(buf, offset)
            if not 8 <= size <= MAX_FRAME_SIZE:
                self._fail(ValueError('invalid frame size'))
                return
            if offset + size > n:
                break
            if not active:
                self._fail(ValueError('traffic before authorized lobby start'))
                return
            if not ((kind_u32 == NBD1_ID and (size == fast1 or size == fast2 or size in valid_nbd1))
                    or (kind_u32 == PIN1_ID and size == 16)
                    or (kind_u32 == PON1_ID and size == 24)):
                self._fail(ValueError('invalid relay data message'))
                return
            if backlog_put is not None:
                try:
                    with memoryview(buf) as mv:
                        backlog_put((now, bytes(mv[offset:offset + size])))
                except asyncio.QueueFull:
                    self._fail(ValueError('fake-latency backlog overflow'))
                    return
            offset += size

        if offset > 0:
            if backlog_put is None:
                with memoryview(buf) as mv:
                    peer_write(mv[:offset])
            if offset == n:
                buf.clear()
            else:
                del buf[:offset]

    def pause_writing(self):
        # Outbound socket buffer to THIS cabinet filled past WRITE_HIGH_WATER;
        # pause reading from the cabinet feeding us until kernel buffer drains.
        upstream = self.inbound_peer
        if upstream is not None and upstream.transport is not None:
            upstream.transport.pause_reading()
        if self._stall_timer is None:
            self._stall_timer = self.loop.call_later(
                STALL_TIMEOUT_SEC, self._on_write_stall)

    def resume_writing(self):
        if self._stall_timer is not None:
            self._stall_timer.cancel()
            self._stall_timer = None
        upstream = self.inbound_peer
        if upstream is not None and upstream.transport is not None:
            upstream.transport.resume_reading()

    def _on_write_stall(self):
        self._stall_timer = None
        self.room.fail('peer write stalled for 30s')
        self._fail(TimeoutError('peer write stalled'))

    def eof_received(self):
        if not self.done.done():
            self.done.set_exception(asyncio.IncompleteReadError(bytes(self._buf), 8))
        return False

    def connection_lost(self, exc):
        if self._stall_timer is not None:
            self._stall_timer.cancel()
            self._stall_timer = None
        if not self.done.done():
            if exc is not None:
                self.done.set_exception(exc)
            else:
                self.done.set_exception(asyncio.IncompleteReadError(bytes(self._buf), 8))
        if self.orig_proto is not None:
            with contextlib.suppress(Exception):
                self.orig_proto.connection_lost(exc)


class Channel:
    __slots__ = ('reader', 'writer', 'hello', 'number', 'peer', 'backlog', 'proto', 'hybrid')

    def __init__(self, reader, writer, hello, number):
        self.reader, self.writer = reader, writer
        self.hello, self.number = hello, number
        self.peer = None
        self.backlog = None  # asyncio.Queue of (arrival, frame) when latency faking is on
        self.proto = None
        self.hybrid = False


class Room:
    def __init__(self, log=print, latency_ms=0, jitter_ms=0, tick_rate=DEFAULT_TICK_RATE, on_change=None, can_gc=None):
        tick_rate = int(tick_rate)
        if not MIN_TICK_RATE <= tick_rate <= MAX_TICK_RATE:
            raise ValueError(f'tick_rate must be {MIN_TICK_RATE}-{MAX_TICK_RATE}')
        self.guid = None
        self.players = {}  # insertion order is cabinet order; nonce joins two sockets
        self.started = False
        self.failed = False
        self.tasks = set()
        self.pumps = set()
        self.connections = set()
        self.log = log
        self.latency = Latency(latency_ms, jitter_ms)
        self.tick_rate = tick_rate
        self.on_change = on_change
        self.can_gc = can_gc
        self.udp = None
        self.udp_sessions = []

    def _notify(self):
        if self.on_change is not None:
            self.on_change()

    def status(self):
        self.log('GAME ' + (GAMES[self.guid][0] if self.guid else 'not selected; first cabinet selects exact ROM'))
        self.log('ROOM ' + ('RUNNING' if self.started and not self.failed else 'RESET REQUIRED' if self.failed else 'WAITING'))
        for i, channels in enumerate(self.players.values()):
            self.log(f'  cabinet {i+1}: {"MASTER" if i == 0 else "SLAVE"}, car {i+1}, channels {len(channels)}/2')
        if not self.players:
            self.log('  no cabinets connected')

    async def start(self):
        if self.started or self.failed:
            self.log('Cannot link: create a new lobby and restart clients first.')
            return False
        max_players = GAME_MAX_PLAYERS.get(GAMES[self.guid][0], MAX_PLAYERS) if self.guid in GAMES else MAX_PLAYERS
        if not 2 <= len(self.players) <= max_players or any(len(c) != 2 for c in self.players.values()):
            self.log(f'Cannot start: need 2-{max_players} cabinets, each with both channels connected.')
            return False
        players = list(self.players.values())
        segment_size = GAMES[self.guid][1]
        fast2 = segment_size + 8
        fast1 = segment_size + 56
        valid_nbd1 = frozenset((8, 9, 10, 16, 56, 64, fast2, fast1))

        # Wire all peers and queue ALL greetings before yielding. Peer greetings,
        # Ping/Pong and board bytes retain their end-to-end meaning and ordering.
        for i, channels in enumerate(players):
            channels[0].peer = players[(i + 1) % len(players)][1]
            channels[1].peer = players[(i - 1) % len(players)][0]

        use_latency = self.latency.enabled
        if use_latency:
            for channels in players:
                for channel in channels.values():
                    channel.backlog = asyncio.Queue(maxsize=Latency.MAX_BACKLOG)

        for channels in players:
            for channel in channels.values():
                peer = channel.peer
                peer_transport = peer.writer.transport
                with contextlib.suppress(Exception):
                    peer_transport.set_write_buffer_limits(high=WRITE_HIGH_WATER, low=WRITE_LOW_WATER)
                if channel.proto is not None:
                    channel.proto.bind_peer(peer_transport, fast1, fast2, valid_nbd1, channel.backlog)
                    if peer.proto is not None:
                        peer.proto.inbound_peer = channel.proto

        # Sweep handshake/lobby allocations right before link activation when no
        # other race room is actively streaming, minimizing in-race GC pauses.
        if self.can_gc is None or self.can_gc():
            gc.collect(0)

        self.started = True
        udp_sessions = self.udp.register(self, players) if self.udp is not None else [None] * len(players)
        if self.udp is not None:
            CONFIG = relay_udp.CONFIG
        tick_pkt = frame(b'RLT1', TICK_RATE.pack(VERSION, self.tick_rate)) if self.tick_rate != DEFAULT_TICK_RATE else b''
        for i, channels in enumerate(players):
            for number, channel in channels.items():
                udp_pkt = b''
                if number == 0 and udp_sessions[i] is not None:
                    udp_kind, udp_config = self.udp.config(self, udp_sessions, i)
                    udp_pkt = frame(udp_kind, udp_config)
                channel.writer.write(channel.peer.hello + tick_pkt + udp_pkt + frame(b'RLA1', READY.pack(VERSION, i, len(players), number)))
            self.log(f'ASSIGN cabinet={i+1} role={"MASTER" if i==0 else "SLAVE"} car={i+1}')
        try:
            for p in players:
                for c in p.values():
                    if c.proto is None:
                        await asyncio.wait_for(c.writer.drain(), 10)
        except (OSError, asyncio.TimeoutError):
            self.fail('start delivery failed')
            return False
        if use_latency:
            # One delivery pump per direction so the read loops stay responsive
            # and pipelined frames keep their original spacing.
            for channels in players:
                for channel in channels.values():
                    task = asyncio.create_task(self.pump(channel))
                    task.add_done_callback(self.pumps.discard)
                    self.pumps.add(task)
            self.log(f'FAKE LATENCY active for this session: {self.latency.describe()}')
        if self.tick_rate != DEFAULT_TICK_RATE:
            self.log(f'TICK RATE active for this session: {self.tick_rate} Hz')
        self.log(f'STARTED {len(players)} cabinets; no late joins or live master migration')
        return True

    async def pump(self, channel):
        """Deliver one direction's frames to the peer after the fake latency."""
        queue, last = channel.backlog, 0.0
        peer = channel.peer
        if peer is None:
            return
        peer_transport = peer.writer.transport
        peer_write = peer_transport.write
        sample = self.latency.sample
        try:
            while True:
                arrival, raw = await queue.get()
                target = arrival + sample()
                # Keep wire order even when jitter would reorder two frames.
                if target < last:
                    target = last
                else:
                    last = target
                now = time.monotonic()
                if target > now:
                    await asyncio.sleep(target - now)
                    now = time.monotonic()
                if self.failed or peer.writer.is_closing():
                    return
                # Coalesce any pipelined frames whose target time has also arrived.
                batch = None
                while not queue.empty():
                    next_arrival, _ = queue._queue[0]
                    next_target = next_arrival + sample()
                    if next_target < last:
                        next_target = last
                    if next_target > now:
                        break
                    last = next_target
                    _, next_raw = queue.get_nowait()
                    if batch is None:
                        batch = bytearray(raw)
                    batch.extend(next_raw)
                peer_write(bytes(batch) if batch is not None else raw)
                if peer_transport.get_write_buffer_size() > WRITE_HIGH_WATER:
                    await asyncio.sleep(0)
        except asyncio.CancelledError:
            return
        except (ConnectionError, asyncio.TimeoutError, OSError):
            self.fail('fake-latency delivery failed')

    def fail(self, reason):
        if self.failed:
            return
        self.failed = True
        if self.udp is not None:
            self.udp.unregister(self)
        self.stop_pumps()
        self.log(f'ROOM FAILED: {reason}. Create a new lobby and restart all clients.')
        err_pkt = frame(b'RLE1', struct.pack('<II', 5, 0))
        for channels in self.players.values():
            for c in channels.values():
                if not c.writer.is_closing():
                    with contextlib.suppress(OSError, RuntimeError):
                        c.writer.write(err_pkt)
                    c.writer.close()
        self._notify()

    def stop_pumps(self):
        for task in tuple(self.pumps):
            task.cancel()
        self.pumps.clear()

    async def reset(self):
        if self.udp is not None:
            self.udp.unregister(self)
        self.stop_pumps()
        writers = [c.writer for channels in self.players.values() for c in channels.values()]
        self.players.clear()
        self.guid = None
        # Old handlers no longer own a roster entry, so their finally blocks
        # cannot fail or remove a newly joined room.
        self.started = self.failed = False
        for w in writers:
            w.close()
        self.log('ROOM CLEARED.')

    async def reject(self, writer, code):
        if not writer.is_closing():
            writer.write(frame(b'HEL1', b'Supermodel relay rejection') + frame(b'RLE1', struct.pack('<II', code, 0)))
            with contextlib.suppress(OSError, asyncio.TimeoutError):
                await asyncio.wait_for(writer.drain(), 2)
        writer.close()

    async def handle(self, reader, writer, initial=None):
        task = asyncio.current_task()
        self.tasks.add(task)
        self.connections.add(writer)
        nonce = channel = None
        try:
            if len(self.connections) > 48:  # two channels x 16 cabinets + bounded handshake headroom
                await self.reject(writer, 1)
                return
            tune_socket(writer)
            kind, _, hello = initial[0] if initial is not None else await asyncio.wait_for(read_frame(reader), 10)
            if kind != b'HEL1':
                await self.reject(writer, 2)
                return
            kind, body, _ = initial[1] if initial is not None else await asyncio.wait_for(read_frame(reader), 10)
            hybrid = kind == b'RLJ3'
            if kind not in (b'RLJ1', b'RLJ3') or len(body) != JOIN.size:
                await self.reject(writer, 2)
                return
            version, number, nonce, guid = JOIN.unpack(body)
            if version != VERSION or guid not in GAMES or (self.guid is not None and guid != self.guid) or number not in (0, 1) or nonce == bytes(16):
                await self.reject(writer, 2)
                return
            if self.started or self.failed:
                await self.reject(writer, 3)
                return
            if nonce not in self.players:
                max_players = GAME_MAX_PLAYERS.get(GAMES[guid][0], MAX_PLAYERS)
                if len(self.players) >= max_players:
                    await self.reject(writer, 1)
                    return
                self.players[nonce] = {}
            self.guid = guid
            channels = self.players[nonce]
            if number in channels:
                await self.reject(writer, 4)
                return
            if any(c.hybrid != hybrid for c in channels.values()):
                await self.reject(writer, 2)
                return
            channel = Channel(reader, writer, hello, number)
            channel.hybrid = hybrid
            transport = writer.transport
            if hasattr(transport, 'set_protocol') and hasattr(transport, 'get_protocol') and hasattr(reader, '_buffer'):
                loop = asyncio.get_running_loop()
                proto = FastChannelProtocol(self, transport, transport.get_protocol(), loop)
                channel.proto = proto
                channels[number] = channel
                position = list(self.players).index(nonce) + 1
                self.log(f'JOIN cabinet={position} channel={number} peer={writer.get_extra_info("peername")}')
                if len(channels) == 2:
                    self.log(f'READY cabinet={position}; waiting for all authorized lobby channels')
                    self._notify()
                leftover = bytes(reader._buffer)
                reader._buffer.clear()
                transport.set_protocol(proto)
                if leftover:
                    proto.data_received(leftover)
                await proto.done
                return

            # Fallback coroutine path for custom/mock transports without set_protocol.
            channels[number] = channel
            position = list(self.players).index(nonce) + 1
            self.log(f'JOIN cabinet={position} channel={number} peer={writer.get_extra_info("peername")}')
            if len(channels) == 2:
                self.log(f'READY cabinet={position}; waiting for all authorized lobby channels')
                self._notify()
            while True:
                kind, body, raw = await read_frame(reader)
                if not self.started or channel.peer is None or self.failed:
                    raise ValueError('traffic before authorized lobby start')
                segment_size = GAMES[self.guid][1]
                timed_segment_size = segment_size + 48
                valid = ((kind == b'PIN1' and len(body) == 8) or
                         (kind == b'PON1' and len(body) == 16) or
                         (kind == b'NBD1' and len(body) in (0, 1, 2, 8, 48, 56, segment_size, timed_segment_size)))
                if not valid:
                    raise ValueError('invalid relay data message')
                if channel.backlog is not None:
                    try:
                        channel.backlog.put_nowait((time.monotonic(), raw))
                    except asyncio.QueueFull:
                        raise ValueError('fake-latency backlog overflow')
                else:
                    peer_writer = channel.peer.writer
                    peer_writer.write(raw)
                    if peer_writer.transport.get_write_buffer_size() > WRITE_HIGH_WATER:
                        await asyncio.wait_for(peer_writer.drain(), 30)
        except (asyncio.IncompleteReadError, ConnectionError, asyncio.TimeoutError, ValueError, OSError) as exc:
            self.log(f'CHANNEL CLOSED: {type(exc).__name__}: {exc}')
        finally:
            channels = self.players.get(nonce)
            if channel is not None and channels is not None and channels.get(channel.number) is channel:
                if self.started:
                    self.fail('a cabinet disconnected or sent invalid traffic')
                else:
                    self.players.pop(nonce, None)
                    if not self.players:
                        self.guid = None
                    for c in channels.values():
                        c.writer.close()
                    self.log('Cabinet left waiting room; remaining join order retained.')
                    self._notify()
            writer.close()
            with contextlib.suppress(OSError, asyncio.TimeoutError):
                await asyncio.wait_for(writer.wait_closed(), 2)
            self.tasks.discard(task)
            self.connections.discard(writer)

    async def close(self):
        pumps = list(self.pumps)
        await self.reset()
        tasks = list(self.tasks)
        for w in list(self.connections):
            w.close()
        for task in tasks:
            task.cancel()
        await asyncio.gather(*tasks, *pumps, return_exceptions=True)


async def warmup_jit(force=False):
    """Pre-compile PyPy3 JIT traces for the event-loop + FastChannelProtocol hot path.

    PyPy's meta-tracing JIT compiles loops/functions around ~1039-1619 iterations.
    Without startup warmup, a 3-cabinet race hits that threshold ~15-25s into the
    very first race on a weak CPU, causing a one-off event-loop stall.
    """
    if not force and getattr(sys, 'implementation', None) and sys.implementation.name != 'pypy':
        return
    with contextlib.suppress(Exception):
        loop = asyncio.get_running_loop()
        room = Room(log=lambda *_: None)
        room.started = True
        fast2 = 3264 + 8
        fast1 = 3264 + 56
        valid = frozenset((8, 9, 10, 16, 56, fast2, fast1))
        pkt = frame(b'NBD1', b'\x5a' * (3264 + 48))
        pin = frame(b'PIN1', b'\0' * 8)
        combo = pkt + pin
        iters = 1700
        lanes = 2
        expected = iters * len(combo) * lanes
        received = 0
        sink_done = loop.create_future()

        class _SinkProto(asyncio.Protocol):
            def data_received(self, data):
                nonlocal received
                received += len(data)
                if received >= expected and not sink_done.done():
                    sink_done.set_result(True)

        transports = []
        protos = []
        srcs = []
        for _ in range(lanes):
            s1, s2 = socket.socketpair()
            s3, s4 = socket.socketpair()
            for s in (s1, s2, s3, s4):
                s.setblocking(False)
            t_in, p_in = await loop.connect_accepted_socket(
                lambda: FastChannelProtocol(room, None, None, loop), s2)
            p_in.transport = t_in
            t_peer, _ = await loop.connect_accepted_socket(asyncio.Protocol, s3)
            t_sink, _ = await loop.connect_accepted_socket(_SinkProto, s4)
            t_src, _ = await loop.connect_accepted_socket(asyncio.Protocol, s1)
            p_in.bind_peer(t_peer, fast1, fast2, valid, None)
            transports.extend((t_src, t_in, t_peer, t_sink))
            protos.append(p_in)
            srcs.append(t_src)
        for i in range(iters):
            if i & 7 == 0:
                for ts in srcs:
                    ts.write(combo[:13])
                await asyncio.sleep(0)
                for ts in srcs:
                    ts.write(combo[13:])
            else:
                for ts in srcs:
                    ts.write(combo)
            await asyncio.sleep(0)
        await asyncio.wait_for(sink_done, 5.0)
        for p_in in protos:
            if not p_in.done.done():
                p_in.done.set_result(None)
        for tr in transports:
            tr.close()
        await asyncio.sleep(0)
        gc.collect()


async def serve(args):
    # Lobby master authorization is the only start mechanism. No stdin thread,
    # console commands, legacy room or command-file control surface.
    _enable_winmm_high_res()
    await warmup_jit()
    Hub = online_lobby.Hub
    protocol = Room(log=lambda line: print(line, flush=True),
                    latency_ms=args.latency_ms, jitter_ms=args.jitter_ms,
                    tick_rate=args.tick_rate)
    hub = Hub(protocol, log=lambda line: print(line, flush=True),
              latency_ms=args.latency_ms, jitter_ms=args.jitter_ms,
              tick_rate=args.tick_rate, autostart=args.autostart)
    server = await asyncio.start_server(hub.handle, args.bind, args.port, limit=65536)
    UdpRelay = relay_udp.UdpRelay
    udp = None
    if not getattr(args, 'tcp_only', False):
        udp = UdpRelay(protocol.latency)
        try:
            await asyncio.get_running_loop().create_datagram_endpoint(lambda: udp, local_addr=(args.bind, args.port), family=socket.AF_INET)
        except BaseException:
            server.close()
            await server.wait_closed()
            raise
    protocol.udp = udp
    hub.start()
    print(f'Async game relay listening on {args.bind}:{args.port}; trusted VPN/firewall only', flush=True)
    print('Use Supermodel Online: create/join a lobby; the master presses Start Link.', flush=True)
    if protocol.latency.enabled:
        print(f'Fake latency enabled: {protocol.latency.describe()}; '
              'game traffic only, roughly twice that in round trip.', flush=True)
    if protocol.tick_rate != DEFAULT_TICK_RATE:
        print(f'Tick rate configured: {protocol.tick_rate} Hz '
              f'(~{1000.0 / protocol.tick_rate:.1f} ms update interval).', flush=True)
    if udp is not None:
        print(f'UDP race lane listening on {args.bind}:{args.port} (default); allow BOTH TCP and UDP on this port.', flush=True)
    else:
        print('UDP race lane disabled by --tcp-only: all game traffic uses the TCP ring.', flush=True)
    print('In-race snapshots use UDP only (TCP stays connected but silent); setup, race start, state changes and '
          'fallback use TCP. Sega Rally 2 keeps the TCP ring during races.', flush=True)
    print('Stop this relay with Ctrl+C.', flush=True)
    try:
        await asyncio.Event().wait()
    finally:
        server.close()
        if udp is not None:
            udp.close()
        # Python 3.13 Server.wait_closed() also waits for client transports.
        # Close active rooms first, otherwise shutdown can wait on itself.
        await hub.close()
        await protocol.close()
        await server.wait_closed()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bind', default='127.0.0.1', help='listen address (default: loopback); use a VPN address or 0.0.0.0 for remote clients')
    parser.add_argument('--port', type=int, default=1970, help='TCP/UDP relay port (default: 1970)')
    parser.add_argument('--tcp-only', action='store_true', help='carry all game traffic on the TCP ring; by default in-race snapshots use UDP and TCP stays silent during races')
    parser.add_argument('--tick-rate', type=int, default=DEFAULT_TICK_RATE, dest='tick_rate',
                        help=f'game network update rate in Hz sent to connected emulators ({MIN_TICK_RATE}-{MAX_TICK_RATE}, default {DEFAULT_TICK_RATE})')
    parser.add_argument('--latency-ms', type=int, default=0, dest='latency_ms',
                        help='fake one-way latency in ms added to relayed game traffic after Start Link (default 0 = off)')
    parser.add_argument('--jitter-ms', type=int, default=0, dest='jitter_ms',
                        help='random +/- jitter in ms applied to --latency-ms; 0 = constant delay')
    parser.add_argument('--autostart', type=int, default=0, dest='autostart', metavar='N',
                        help='test/CI only: launch a lobby automatically once N cabinets have joined '
                             '(0 = normal, master presses Start Link)')
    args = parser.parse_args()
    if not 0 <= args.autostart <= 16:
        parser.error('--autostart must be 0-16')
    if not 1 <= args.port <= 65535:
        parser.error('--port must be 1-65535')
    if not MIN_TICK_RATE <= args.tick_rate <= MAX_TICK_RATE:
        parser.error(f'--tick-rate must be {MIN_TICK_RATE}-{MAX_TICK_RATE}')
    if not 0 <= args.latency_ms <= 5000:
        parser.error('--latency-ms must be 0-5000')
    if not 0 <= args.jitter_ms <= 5000:
        parser.error('--jitter-ms must be 0-5000')
    try:
        asyncio.run(serve(args))
    except KeyboardInterrupt:
        pass


# Namespaces isolate the original UDP and lobby globals (HEADER, Room, GAMES,
# etc.). They are compiled once at import/startup, never per tick or packet.
# Sources are embedded as plain readable text, not downloaded or unpacked.
import types as _types

def _bundled_module(name,source,bindings=None):
    module=_types.ModuleType(name)
    module.__file__=__file__+'['+name+']'
    if bindings:
        module.__dict__.update(bindings)
    exec(compile(source,module.__file__,'exec'),module.__dict__)
    return module

relay_udp=_bundled_module('relay_udp',r'''
#!/usr/bin/env python3
"""Bounded UDP race-snapshot lane. GPL-3.0-or-later.

TCP owns authorization, link setup, race-start control, chat and session
lifetime. During a race each cabinet sends its whole snapshot by UDP only
(latest wins, never resent) and the TCP ring stays silent except for a
keyframe when race state changes. Clients fall back to the TCP ring by
themselves whenever any cabinet stops confirming UDP delivery. Bearer
cookies protect against off-path injection, NOT eavesdropping/on-path attackers.
Use the same trusted VPN/firewall as the TCP relay. Standard library / PyPy3.
"""
import asyncio
import secrets
import struct
import time
import socket

HEADER = struct.Struct('<4sBBBB16sIHHQ')
DATAGRAM = 1200
CHUNK = DATAGRAM - HEADER.size
# Largest native segment (3264) + 16-byte client trailer + 1 encoding byte.
MAX_ENCODED = 3281
WIRE_VERSION = 2
ACK_CLOCK = struct.Struct('<Q')  # relay monotonic microseconds appended to every ACK
HELLO, ACK, SNAPSHOT, HOST_STATUS, ROUTE = 1, 2, 3, 4, 5
CONFIG = struct.Struct('<IIII16s')


def parse(data):
    if not HEADER.size <= len(data) <= DATAGRAM:
        return None
    h = HEADER.unpack_from(data)
    magic, version, kind, part, parts, token, seq, origin, total, stamp = h
    if magic != b'UGP1' or version != WIRE_VERSION:
        return None
    if kind == ACK:
        return h if len(data) == HEADER.size + ACK_CLOCK.size and not (part or parts or total) else None
    if kind in (HELLO, HOST_STATUS, ROUTE):
        return h if len(data) == HEADER.size and not (part or parts or total) else None
    if kind != SNAPSHOT or not 2 <= total <= MAX_ENCODED or not 0 < stamp <= 0x7fffffffffffffff:
        return None
    expected_parts = (total + CHUNK - 1) // CHUNK
    if parts != expected_parts or part >= parts or len(data) != HEADER.size + min(CHUNK, total - part * CHUNK):
        return None
    return h


class Session:
    __slots__ = ('room', 'index', 'token', 'ip', 'address', 'last_hello', 'credit', 'credit_at', 'rate', 'last_seq', 'seen', 'stamp', 'total', 'parts', 'mask', 'host_ready', 'status_at', 'status_stamp')
    def __init__(self, room, index, ip):
        self.room, self.index, self.ip = room, index, ip
        self.token = secrets.token_bytes(16)
        self.address = None
        self.last_hello = 0.0
        self.rate = (MAX_ENCODED + 3 * HEADER.size) * room.tick_rate
        self.credit = self.rate // 4 + MAX_ENCODED
        self.credit_at = time.monotonic()
        self.last_seq = self.mask = self.stamp = self.total = self.parts = 0
        self.seen = False
        self.host_ready = self.status_at = 0.0
        self.status_stamp = 0


class UdpRelay(asyncio.DatagramProtocol):
    def __init__(self, latency=None):
        self.transport = None
        self.sessions = {}
        self.latency = latency
        self.pending = 0
        self.accepted = self.forwarded = self.dropped = 0
        self.bytes_in = self.bytes_out = 0
        self.port = 0

    def connection_made(self, transport):
        self.transport = transport
        self.port = transport.get_extra_info('sockname')[1]

    def register(self, room, channels):
        # Called only after the authorized TCP room roster is fixed.
        sessions = []
        for index, pair in enumerate(channels):
            if not getattr(pair[0], 'hybrid', False):
                sessions.append(None)
                continue
            peer = pair[0].writer.get_extra_info('peername')
            session = Session(room, index, peer[0])
            while session.token in self.sessions:
                session.token = secrets.token_bytes(16)
            self.sessions[session.token] = session
            sessions.append(session)
        room.udp_sessions = sessions
        return sessions

    def config(self, room, sessions, index):
        s = sessions[index]
        base = CONFIG.pack(1, self.port, index, len(sessions), s.token)
        port = getattr(room, 'host_udp_port', 0)
        if not port or any(peer is None for peer in sessions):
            return b'RLU1', base
        # Public IP is derived from the authenticated creator TCP connection,
        # never a client-supplied arbitrary forwarding destination.
        host = socket.inet_aton(sessions[0].ip)
        extra = host + struct.pack('<III', port, room.tick_rate, len(sessions) if index == 0 else 0)
        if index == 0:
            extra += b''.join(socket.inet_aton(peer.ip) + peer.token for peer in sessions)
        return b'RLU2', base + extra

    def routes(self, room, now, requestor=None):
        sessions = room.udp_sessions
        # All cabinets must prove recent host reachability, not just the creator.
        # A short, repeatedly renewed route lease makes fallback room-wide.
        active = bool(getattr(room, 'host_udp_port', 0)) and all(
            s is not None and s.address is not None and now - s.last_hello < 2.0
            and s.host_ready and now - s.host_ready < 1.5 for s in sessions)
        changed = active != getattr(room, 'host_udp_active', False)
        if changed:
            room.host_udp_active = active
            room.log('UDP route: ' + ('creator-hosted (all cabinets reachable)' if active else 'central relay fallback'))
        room.host_route_serial = (getattr(room, 'host_route_serial', 0) + 1) & 0xffffffff
        # O(N) heartbeats, not O(N^2) fanout: broadcast only on route changes.
        for dest in (sessions if changed or requestor is None else (requestor,)):
            if dest is not None and dest.address is not None and now - dest.last_hello < 2.0:
                self.transport.sendto(HEADER.pack(b'UGP1', WIRE_VERSION, ROUTE, 0, 0, dest.token,
                    room.host_route_serial, dest.index, 0, int(active)), dest.address)

    def unregister(self, room):
        for s in getattr(room, 'udp_sessions', ()):
            if s is not None:
                self.sessions.pop(s.token, None)
        room.udp_sessions = []

    def _send(self, room, token, packet, address):
        self.pending -= 1
        s = self.sessions.get(token)
        if s is not None and s.room is room and room.started and not room.failed and s.address == address:
            self.transport.sendto(packet, address)
            self.forwarded += 1
            self.bytes_out += len(packet)

    def datagram_received(self, data, address):
        h = parse(data)
        if h is None:
            self.dropped += 1
            return
        _, version, kind, part, parts, token, seq, origin, total, stamp = h
        s = self.sessions.get(token)
        if s is None or s.room.failed or not s.room.started or origin != s.index:
            self.dropped += 1
            return
        now = time.monotonic()
        if kind == HELLO:
            # UDP NAT rebinding is allowed only with the session cookie and the
            # same public IP as the authorized TCP connection. No data fanout
            # until endpoint proof; ACK is no larger than the request.
            if address[0] != s.ip or now - s.last_hello < 0.1:
                self.dropped += 1
                return
            s.address, s.last_hello = address, now
            # The relay clock lets cabinets measure each other's snapshot age.
            self.transport.sendto(HEADER.pack(b'UGP1', WIRE_VERSION, ACK, 0, 0, token, seq, origin, 0, stamp)
                                  + ACK_CLOCK.pack(time.monotonic_ns() // 1000), address)
            return
        if kind == HOST_STATUS:
            if (not getattr(s.room, 'host_udp_port', 0) or address != s.address
                    or now - s.last_hello > 2.0 or seq not in (0, 1) or not 0 < stamp <= 0x7fffffffffffffff
                    or stamp <= s.status_stamp or now - s.status_at < 0.1):
                self.dropped += 1
                return
            s.status_at = now
            s.status_stamp = stamp
            s.host_ready = now if seq else 0.0
            self.routes(s.room, now, s)
            return
        if kind != SNAPSHOT or address != s.address or now - s.last_hello > 3.0:
            self.dropped += 1
            return
        s.credit = min(s.rate // 4 + MAX_ENCODED, s.credit + max(0.0, now - s.credit_at) * s.rate)
        s.credit_at = now
        if s.credit < len(data):
            self.dropped += 1
            return
        s.credit -= len(data)
        if s.seen and seq != s.last_seq and not 0 < ((seq - s.last_seq) & 0xffffffff) < 0x80000000:
            self.dropped += 1
            return
        if not s.seen or seq != s.last_seq:
            s.last_seq, s.seen, s.mask = seq, True, 0
            s.stamp, s.total, s.parts = stamp, total, parts
        if (stamp, total, parts) != (s.stamp, s.total, s.parts) or s.mask & (1 << part):
            self.dropped += 1
            return
        s.mask |= 1 << part
        self.accepted += 1
        self.bytes_in += len(data)
        body = data[HEADER.size:]
        loop = None
        for dest in s.room.udp_sessions:
            if dest is None or dest is s or dest.address is None or now - dest.last_hello > 3.0:
                continue
            packet = HEADER.pack(b'UGP1', WIRE_VERSION, SNAPSHOT, part, parts, dest.token, seq, origin, total, stamp) + body
            if self.latency is not None and self.latency.enabled:
                if self.pending >= 4096:
                    self.dropped += 1
                    continue
                if loop is None:
                    loop = asyncio.get_running_loop()
                self.pending += 1
                loop.call_later(self.latency.sample(), self._send, s.room, dest.token, packet, dest.address)
            else:
                self.transport.sendto(packet, dest.address)
                self.forwarded += 1
                self.bytes_out += len(packet)

    def error_received(self, exc):
        # ICMP for a lost/unreachable endpoint must not fail unrelated rooms.
        self.dropped += 1

    def close(self):
        self.sessions.clear()
        if self.transport is not None:
            self.transport.close()

''')

online_lobby=_bundled_module('online_lobby',r'''
#!/usr/bin/env python3
"""GUI lobby control on the existing relay TCP port. GPL-3.0-or-later.

This is a module used by relay.py, not a second server to start.
Trusted VPN only: capability tickets prevent accidental/cross-room registration,
not eavesdropping or access by untrusted users. No ROMs are transferred.
"""
import asyncio
import contextlib
import secrets
import struct
import time
import socket

# Stable GUI IDs, NOT alphabetic order or packet GUID arithmetic.
GAMES = [("daytona2", 0x5bf177da34878), ("dayto2pe", 0x5bf177da34875),
         ("scud", 0x5bf177da34876), ("scudplus", 0x5bf177da34877),
         ("srally2", 0x5bf177da34879), ("lemans24", 0x5bf177da3487a), ("srally2_8p", 0x5bf177da3487b)]
GAME_MAX_PLAYERS = {"daytona2": 16, "dayto2pe": 16, "srally2": 4, "srally2_8p": 8, "lemans24": 6}


def max_players_for_game(game):
    return GAME_MAX_PLAYERS.get(GAMES[game][0], 8)
HELLO = struct.Struct('<III32s')  # version, locally available ROM mask, explicit AsyncNet opt-in, name
CREATE = struct.Struct('<I64s')
CREATE2 = struct.Struct('<I64sII')  # game, title, UDP mode (0=central, 1=creator), UDP port
ROOM = struct.Struct('<IIIII64s')  # id, game, state, count, creator id, title
MEMBER = struct.Struct('<I32s')
LAUNCH = struct.Struct('<IIII16s')  # room, game, reserved index, count, one-session ticket
JOIN2 = struct.Struct('<II16sQ16s')
CHAT_SEND = struct.Struct('<160s')
CHAT_MSG = struct.Struct('<III32s160s')  # seq, scope (0=lounge, >0=room.id), car (0..16), sender, text
CHAT_JOIN = struct.Struct('<I16s32s')  # version, one-session ticket, fallback name
CHAT_WELCOME = struct.Struct('<III32s64s')  # room, car (1..16), count, assigned name, room title
MAX_LOUNGE_CHAT = 32
MAX_ROOM_CHAT = 64


def text(raw, fallback):
    value = raw.split(b'\0', 1)[0].decode('ascii', 'replace')
    return ''.join(c for c in value if 32 <= ord(c) < 127).strip() or fallback


def fixed(value, size):
    return value.encode('ascii', 'replace')[:size-1].ljust(size, b'\0')


def chat_text(raw, fallback=''):
    decoded = raw.split(b'\0', 1)[0].decode('utf-8', 'replace')
    cleaned = ''.join(' ' if c in '\t\r\n' else c for c in decoded if ord(c) >= 32 and ord(c) != 127).strip()
    return cleaned or fallback


def chat_fixed(value, size):
    encoded = value.encode('utf-8', 'replace')
    if len(encoded) >= size:
        encoded = encoded[:size - 1]
        while encoded and (encoded[-1] & 0xC0) == 0x80:
            encoded = encoded[:-1]
        if encoded and encoded[-1] >= 0xC0:
            encoded = encoded[:-1]
        encoded = encoded.decode('utf-8', 'ignore').encode('utf-8')
    return encoded[:size - 1].ljust(size, b'\0')


def native_car_limit(game):
    return 16 if game < 2 else 8 if game < 4 else 0


def valid_room_options(game, value):
    if not 0 <= game < 7 or value & ~1023 or (value & 1 and game not in (0, 3)):
        return False
    base = 4 if game == 6 else game
    if value & 768 and base >= 4:
        return False
    if not value & 2:
        return not value & ~769
    game = base
    return game < 5 and ((value >> 2) & 7) < (5 if game == 4 else 4) and ((value >> 5) & 7) < (7 if game < 2 else 3 if game < 4 else 2)


def all_native(room):
    return all(m.get('native_capable') for m in room.members)


def first_free_car(room):
    used = {m.get('native_car', 0) for m in room.members}
    return next((car for car in range(1, native_car_limit(room.game) + 1) if car not in used), 0)


class Lobby:
    def __init__(self, ident, game, title, owner, log, latency_ms=0, jitter_ms=0, tick_rate=DEFAULT_TICK_RATE, on_change=None, can_gc=None):
        self.id, self.game, self.title = ident, game, title
        self.members = [owner]
        self.state = 0  # waiting / launching / linked / failed
        self.options = (2 | (4 if game < 4 else 0)) if (game < 5 or game == 6) and owner.get('native_capable') else 0
        owner['native_car'] = 1 if native_car_limit(game) else 0
        self.frozen_native_cars = {}
        self.host_mode = self.host_port = 0
        self.host_checking = False
        self.host_deadline = self.host_last = 0.0
        self.host_probe = b''
        self.host_task = None
        self.host_launch_pending = False
        self.directory_ready = False
        self.listed_count = 1
        self.deadline = 0
        self.tickets = {}
        self.ticket_meta = {}  # ticket -> (car_number_1based, player_name)
        self.chat = []  # (seq, car, sender, message)
        self.next_chat_seq = 1
        self.rank_updates = {}  # ticket -> (rank, monotonic timestamp, writer)
        self.chat_clients = {}  # writer -> client dict
        # Fake latency and tick rate are relay-wide settings; every lobby's data link inherits them.
        self.board = Room(log=lambda line: log(f'LOBBY {ident}: {line}'),
                          latency_ms=latency_ms, jitter_ms=jitter_ms, tick_rate=tick_rate,
                          on_change=on_change, can_gc=can_gc)


class Hub:
    def __init__(self, legacy, log=print, launch_timeout=90, latency_ms=0, jitter_ms=0, tick_rate=DEFAULT_TICK_RATE, autostart=0):
        self.legacy, self.log, self.launch_timeout = legacy, log, launch_timeout
        self.autostart = autostart  # 0 = manual Start Link (normal operation)
        self.latency_ms, self.jitter_ms = latency_ms, jitter_ms
        self.tick_rate = tick_rate
        self.clients, self.rooms, self.tickets = {}, {}, {}
        self.next_client = self.next_room = 1
        self.lounge_chat = []  # (seq, 0, sender, message)
        self.next_lounge_seq = 1
        self.connections, self.tasks = set(), set()
        self.monitor_task = None
        self._wakeup = None

    def start(self):
        self._wakeup = asyncio.Event()
        self.monitor_task = asyncio.create_task(self.monitor())

    def notify(self):
        if self._wakeup is not None:
            self._wakeup.set()

    def _can_gc(self):
        return not any(r.state == 2 for r in self.rooms.values())

    def send(self, client, kind, payload=b''):
        w = client['writer']
        if w.is_closing():
            return
        if w.transport.get_write_buffer_size() > 65536:
            w.close()  # a slow GUI must not accumulate unbounded snapshots
            return
        w.write(frame(kind, payload))

    def send_chat(self, client, seq, scope, car, sender, message):
        self.send(client, b'OLM1', CHAT_MSG.pack(seq, scope, car, chat_fixed(sender, 32), chat_fixed(message, 160)))

    def replay_lounge_chat(self, client):
        for seq, car, sender, message in self.lounge_chat:
            self.send_chat(client, seq, 0, car, sender, message)

    def replay_room_chat(self, client, room):
        for seq, car, sender, message in room.chat:
            self.send_chat(client, seq, room.id, car, sender, message)

    def post_lounge_chat(self, sender, message):
        if not message:
            return
        seq = self.next_lounge_seq
        self.next_lounge_seq += 1
        self.lounge_chat.append((seq, 0, sender, message))
        if len(self.lounge_chat) > MAX_LOUNGE_CHAT:
            del self.lounge_chat[:-MAX_LOUNGE_CHAT]
        for peer in tuple(self.clients.values()):
            if peer['room'] == 0:
                self.send_chat(peer, seq, 0, 0, sender, message)

    def post_room_chat(self, room, car, sender, message):
        if not message:
            return
        seq = room.next_chat_seq
        room.next_chat_seq += 1
        room.chat.append((seq, car, sender, message))
        if len(room.chat) > MAX_ROOM_CHAT:
            del room.chat[:-MAX_ROOM_CHAT]
        for member in tuple(room.members):
            if member['id'] in self.clients and member['room'] == room.id:
                self.send_chat(member, seq, room.id, car, sender, message)
        for cc in tuple(room.chat_clients.values()):
            self.send_chat(cc, seq, room.id, car, sender, message)

    def error(self, c, message):
        self.send(c, b'OLE1', fixed(message, 160))

    def snapshot(self, c):
        rooms = [r for r in self.rooms.values() if r.host_mode != 3 or c.get('room_host_capable')]
        self.send(c, b'OLB1', struct.pack('<III', len(rooms), c['id'], c['room']))
        for room in rooms:
            # Discovery metadata only, not the hosted room's membership/names.
            members = room.members
            if room.host_mode == 3 and room.directory_ready:
                members = [dict(id=room.members[0]['id'] + i, name='Creator' if i == 0 else 'Player') for i in range(room.listed_count)]
            body = ROOM.pack(room.id, room.game, room.state, len(members), room.members[0]['id'], fixed(room.title, 64))
            body += b''.join(MEMBER.pack(m['id'], fixed(m['name'], 32)) for m in members)
            self.send(c, b'OLR1', body)
            if c.get('full_host_capable'):
                port = room.host_port if room.host_mode in (2, 3) else getattr(room.board, 'host_udp_port', 0)
                mode = room.host_mode or (1 if port else 0)
                self.send(c, b'OLU2', struct.pack('<IIII', room.id, port, mode, int(room.host_checking)))
            elif c.get('host_capable'):
                self.send(c, b'OLU1', struct.pack('<III', room.id, getattr(room.board, 'host_udp_port', 0), 1 if getattr(room.board, 'host_udp_port', 0) else 0))
            if c.get('options_capable'):
                self.send(c, b'ORO1', struct.pack('<II', room.id, room.options))
            if c.get('native_capable') and not (room.host_mode == 3 and room.directory_ready):
                enabled = all_native(room)
                body = struct.pack('<III', room.id, int(enabled), len(members))
                body += b''.join(struct.pack('<II', m['id'], (room.frozen_native_cars.get(m['id'], 0) if room.state else m.get('native_car', 0)) if enabled else 0) for m in members)
                self.send(c, b'ORN1', body)
        self.send(c, b'OLZ1')

    def broadcast(self):
        for c in tuple(self.clients.values()):
            self.snapshot(c)

    async def destroy(self, room, reason):
        if self.rooms.pop(room.id, None) is None:
            return
        if room.host_mode == 3 and room.directory_ready:
            # Expiring/removing a directory listing never stops a native room.
            await room.board.close()
            self.broadcast()
            return
        if room.host_task is not None and room.host_task is not asyncio.current_task():
            room.host_task.cancel()
            await asyncio.gather(room.host_task, return_exceptions=True)
        self.send(room.members[0], b'OHS1', struct.pack('<I', room.id)) if room.members[0].get('full_host_capable') else None
        for ticket in room.tickets.values():
            self.tickets.pop(ticket, None)
        for cc in tuple(room.chat_clients.values()):
            with contextlib.suppress(OSError, RuntimeError):
                if not cc['writer'].is_closing():
                    cc['writer'].write(frame(b'RLE1', struct.pack('<II', 5, 0)))
            cc['writer'].close()
        room.chat_clients.clear()
        for member in room.members:
            member['room'] = 0
            self.error(member, reason)
        await room.board.close()
        self.broadcast()

    async def leave(self, c):
        room = self.rooms.get(c['room'])
        if room is None:
            c['room'] = 0
            return
        if room.state != 0:
            # The GUI deliberately closes after receiving its launch ticket.
            # Board channels, not the launcher socket, own the running session.
            return
        if room.members[0] is c:
            await self.destroy(room, 'The master left. Create or join another lobby.')
        else:
            room.members.remove(c)
            c['room'] = 0
            self.broadcast()

    def send_launch(self, member, room, index, endpoint=None):
        payload = LAUNCH.pack(room.id, room.game, index, len(room.members), room.tickets[member['id']])
        kind = b'OLG1'
        if endpoint is not None:
            payload += socket.inet_aton(endpoint[0]) + struct.pack('<I', endpoint[1])
            kind = b'OLG2'
        if member.get('options_capable'):
            payload += struct.pack('<I', room.options)
            kind = b'OLG4' if endpoint is not None else b'OLG3'
        if member.get('native_capable'):
            payload += struct.pack('<I', room.frozen_native_cars.get(member['id'], 0))
            kind = b'OLG6' if endpoint is not None else b'OLG5'
        self.send(member, kind, payload)

    def authorize(self, room, reason):
        """Reserve cabinet order and hand every member its one-session ticket."""
        if room.host_mode == 3 or room.host_checking or room.state != 0 or not 2 <= len(room.members) <= max_players_for_game(room.game):
            return False
        if all_native(room) and native_car_limit(room.game) and any(not m.get('native_car') for m in room.members):
            self.error(room.members[0], 'Every player must select a unique CAR NUMBER before Start Link.')
            return False
        room.frozen_native_cars = {m['id']: m.get('native_car', 0) if all_native(room) else 0 for m in room.members}
        room.state = 1
        room.deadline = time.monotonic() + self.launch_timeout
        room.board.guid = GAMES[room.game][1]
        # Reserve native cabinet order BEFORE any emulator sockets arrive.
        # Fast slaves cannot become MASTER by completing their ROM load first.
        for index, member in enumerate(room.members):
            ticket = secrets.token_bytes(16)
            while ticket in self.tickets or ticket == bytes(16):
                ticket = secrets.token_bytes(16)
            room.tickets[member['id']] = ticket
            room.ticket_meta[ticket] = (index + 1, member['name'])
            room.board.players[ticket] = {}
            self.tickets[ticket] = room
            if room.host_mode != 2:
                self.send_launch(member, room, index)
        if room.host_mode == 2:
            room.host_launch_pending = True
            body = struct.pack('<IIII', room.id, room.game, self.tick_rate, len(room.members))
            body += b''.join(room.tickets[m['id']] + secrets.token_bytes(16) for m in room.members)
            self.send(room.members[0], b'OHR1', body)
        self.log(f'LOBBY {room.id}: {reason} for {len(room.members)} cabinets ({GAMES[room.game][0]})')
        self.broadcast()
        self.notify()
        return True

    async def host_fallback(self, room, reason):
        if self.rooms.get(room.id) is not room or room.state != 0 or room.host_mode not in (2, 3):
            return
        room.host_mode = room.host_port = 0
        room.host_checking = False
        room.board.udp = self.legacy.udp
        self.send(room.members[0], b'OHS1', struct.pack('<I', room.id))
        self.post_room_chat(room, 0, 'System', 'Self-host failed (' + reason + '). Switched back to relay hosting.')
        self.broadcast()
        self.notify()

    async def test_host(self, room):
        token = room.host_probe
        packet = frame(b'PHQ1', token)
        target = (room.members[0]['ip'], room.host_port)
        class Probe(asyncio.DatagramProtocol):
            def __init__(self): self.ok = False
            def datagram_received(self, data, address):
                if address == target and data == frame(b'PHA1', token): self.ok = True
        probe = Probe()
        transport = None
        try:
            transport, _ = await asyncio.get_running_loop().create_datagram_endpoint(lambda: probe, local_addr=('0.0.0.0', 0), family=socket.AF_INET)
            while self.rooms.get(room.id) is room and room.host_checking and time.monotonic() < room.host_deadline:
                probe.ok = False
                transport.sendto(packet, target)
                tcp_ok = False
                writer = None
                try:
                    reader, writer = await asyncio.wait_for(asyncio.open_connection(*target), 1.0)
                    tune_socket(writer)
                    writer.write(packet)
                    kind, body, _ = await asyncio.wait_for(read_frame(reader), 1.0)
                    tcp_ok = kind == b'PHA1' and body == token
                except (OSError, ValueError, asyncio.TimeoutError, asyncio.IncompleteReadError):
                    pass
                finally:
                    if writer is not None: writer.close()
                if not probe.ok: await asyncio.sleep(.25)
                if tcp_ok and probe.ok:
                    room.host_checking = False
                    if room.host_mode == 3:
                        room.directory_ready = True
                        room.host_last = time.monotonic()
                        self.handoff(room.members[0], room, owner=True)
                        room.members[0]['room'] = 0
                    else:
                        self.post_room_chat(room, 0, 'System', 'Self-host ready: TCP and UDP reachable. Gameplay will use the creator; room/chat stay central.')
                    self.broadcast()
                    self.notify()
                    if self.autostart and len(room.members) >= min(self.autostart, max_players_for_game(room.game)):
                        self.authorize(room, 'self-host verified; autostart')
                    return
                await asyncio.sleep(.25)
            await self.host_fallback(room, 'TCP/UDP reachability test timed out')
        except (OSError, ValueError):
            await self.host_fallback(room, 'reachability test unavailable')
        finally:
            if transport is not None: transport.close()

    def handoff(self, client, room, owner=False):
        ip = '127.0.0.1' if owner else room.members[0]['ip']
        body = struct.pack('<IIII', room.id, room.game, self.tick_rate, room.host_port)
        body += socket.inet_aton(ip) + fixed(room.title, 64) + (room.host_probe if owner else bytes(16))
        self.send(client, b'OHD1', body)

    def directory_advertisement(self, writer, body):
        if len(body) != 28: raise ValueError('directory advertisement size')
        ident, secret, state, count = struct.unpack('<I16sII', body)
        room = self.rooms.get(ident)
        if room is None or room.host_mode != 3 or not room.directory_ready or not secrets.compare_digest(secret, room.host_probe) or writer.get_extra_info('peername')[0] != room.members[0]['ip']:
            raise ValueError('unauthorized directory advertisement')
        if state > 3 or not 1 <= count <= max_players_for_game(room.game): raise ValueError('directory bounds')
        changed = room.state != state or room.listed_count != count
        room.state, room.listed_count, room.host_last = state, count, time.monotonic()
        writer.write(frame(b'ODK1'))
        if changed: self.broadcast()
        self.notify()

    def host_failed(self, room, reason):
        if room.state not in (1, 2): return
        room.state, room.deadline = 3, time.monotonic() + 15
        self.post_room_chat(room, 0, 'System', reason + ' Recreate the room; live host migration is not supported.')
        for cc in tuple(room.chat_clients.values()):
            self.send(cc, b'RLE1', struct.pack('<II', 5, 0))
        self.broadcast()
        self.notify()

    async def command(self, c, kind, body):
        if kind == b'ORO1' and len(body) == 8:
            ident, value = struct.unpack('<II', body)
            room = self.rooms.get(ident)
            if (room is None or c['room'] != ident or room.members[0] is not c or room.state != 0
                    or room.host_checking or not c.get('options_capable') or not valid_room_options(room.game, value)
                    or (value & 2 and (not c.get('native_capable') or not all_native(room)))):
                self.error(c, 'Only the master can change valid room options before launch; update all clients for native settings.')
                return
            if value and not all(m.get('options_capable') for m in room.members):
                self.error(c, 'Update all room members before enabling room options.')
                return
            if room.game == 0 and value & 1 and not all(m.get('hornet_capable') for m in room.members):
                self.error(c, 'Update all room members before enabling Hornet.')
                return
            if room.game < 4 and value & 512 and not all(m.get('race_capable') for m in room.members):
                self.error(c, 'Update all room members before enabling Real players only.')
                return
            if room.game < 4 and value & 256 and not all(m.get('race_capable') for m in room.members):
                self.error(c, 'Update all room members before disabling collisions.')
                return
            room.options = value
            self.broadcast()
            return
        if kind == b'OCN1' and len(body) == 8:
            ident, car = struct.unpack('<II', body)
            room = self.rooms.get(ident)
            if (room is None or c['room'] != ident or c not in room.members or room.state or room.host_checking
                    or not c.get('native_capable') or not all_native(room) or not native_car_limit(room.game)
                    or car > native_car_limit(room.game)):
                self.error(c, 'CAR NUMBER can only be selected before launch with updated clients.')
                return
            if car and any(m is not c and m.get('native_car') == car for m in room.members):
                self.error(c, 'That CAR NUMBER is already selected by another player.')
                return
            c['native_car'] = car
            self.broadcast()
            return
        if kind in (b'OHB1', b'OHA1') and len(body) == 8:
            ident, ok = struct.unpack('<II', body)
            room = self.rooms.get(ident)
            if room is None or room.members[0] is not c or c['room'] != ident or ok not in (0, 1):
                raise ValueError('unauthorized host response')
            if room.host_mode not in (2, 3): return  # late reply after a creation-time fallback
            if kind == b'OHB1':
                if room.state != 0 or not room.host_checking or room.host_task is not None: raise ValueError('unexpected host bind response')
                if not ok: await self.host_fallback(room, 'host could not bind TCP/UDP port')
                else: room.host_task = asyncio.create_task(self.test_host(room))
            else:
                if room.state != 1 or not room.host_launch_pending: raise ValueError('unexpected host roster response')
                if not ok:
                    room.host_mode = room.host_port = 0
                    room.host_launch_pending = False
                    room.board.udp = self.legacy.udp
                    self.send(c, b'OHS1', struct.pack('<I', room.id))
                    self.post_room_chat(room, 0, 'System', 'Self-host failed during launch preparation. Switched back to relay hosting.')
                    for index, member in enumerate(room.members):
                        self.send_launch(member, room, index)
                    self.broadcast()
                    self.notify()
                    return
                room.host_launch_pending = False
                for index, member in enumerate(room.members):
                    host = '127.0.0.1' if index == 0 else room.members[0]['ip']
                    self.send_launch(member, room, index, (host, room.host_port))
            self.notify()
            return
        if kind == b'OLQ1' and not body:
            self.snapshot(c)
            return
        if kind == b'OLL1' and not body:
            await self.leave(c)
            self.snapshot(c)
            self.replay_lounge_chat(c)
            return
        if kind == b'OLS1' and len(body) == CHAT_SEND.size:
            message = chat_text(body)
            if not message:
                return
            if not c['room']:
                self.post_lounge_chat(c['name'], message)
            else:
                room = self.rooms.get(c['room'])
                if room is not None:
                    car = room.members.index(c) + 1 if c in room.members else 0
                    self.post_room_chat(room, car, c['name'], message)
            return
        if kind in (b'OLC1', b'OLC2', b'OLC3', b'OLC4', b'OLJ1'):
            if c['room']:
                self.error(c, 'Leave your current lobby first.')
                return
            if not c['async']:
                self.error(c, 'AsyncNet must be explicitly enabled before joining.')
                return
            if (kind == b'OLC1' and len(body) == CREATE.size) or (kind in (b'OLC2', b'OLC3', b'OLC4') and len(body) == CREATE2.size):
                mode, host_port = 0, 0
                if kind in (b'OLC2', b'OLC3', b'OLC4'):
                    game, title, mode, host_port = CREATE2.unpack(body)
                    if not c.get('host_capable') or mode not in ((0, 3) if kind == b'OLC4' else (0, 2) if kind == b'OLC3' else (0, 1)) or (kind == b'OLC3' and not c.get('full_host_capable')) or (kind == b'OLC4' and not c.get('room_host_capable')) or (mode and not 1024 <= host_port <= 65535) or (mode == 0 and host_port):
                        raise ValueError('invalid UDP hosting selection')
                    if mode == 1 and self.legacy.udp is None:
                        self.error(c, 'Self-hosted UDP is unavailable while the relay uses --tcp-only.')
                        return
                else:
                    game, title = CREATE.unpack(body)
                if game >= len(GAMES) or not c['mask'] & (1 << game):
                    self.error(c, 'This exact ROM (and required parent) is not available locally.')
                    return
                if len(self.rooms) >= 16:
                    self.error(c, 'Relay lobby limit reached (16).')
                    return
                room = Lobby(self.next_room, game, text(title, c['name'] + "'s lobby"), c, self.log,
                             latency_ms=self.latency_ms, jitter_ms=self.jitter_ms, tick_rate=self.tick_rate,
                             on_change=self.notify, can_gc=self._can_gc)
                room.board.udp = self.legacy.udp
                room.board.host_udp_port = host_port if mode == 1 else 0
                if mode in (2, 3):
                    room.host_mode, room.host_port, room.host_checking = mode, host_port, True
                    room.host_probe = secrets.token_bytes(16)
                    room.host_deadline = time.monotonic() + 25
                    room.board.udp = None
                self.next_room += 1
                self.rooms[room.id] = room
            elif kind == b'OLJ1' and len(body) == 4:
                room = self.rooms.get(struct.unpack('<I', body)[0])
                if room is None or room.state != 0 or (room.listed_count if room.host_mode == 3 else len(room.members)) >= max_players_for_game(room.game):
                    self.error(c, 'Lobby unavailable, already started, or full.')
                    return
                if not c['mask'] & (1 << room.game):
                    self.error(c, 'Install the selected ROM (and parent) in your ROM folder first.')
                    return
                if getattr(room.board, 'host_udp_port', 0) and not c.get('host_capable'):
                    self.error(c, 'This room uses creator-hosted UDP. Update your emulator to r16 or newer.')
                    return
                if room.host_mode == 2 and not c.get('full_host_capable'):
                    self.error(c, 'This room self-hosts TCP and UDP gameplay. Update to r17 or newer.')
                    return
                if room.host_mode == 3:
                    if not c.get('room_host_capable') or not room.directory_ready:
                        self.error(c, 'Hosted room unavailable or client needs r18 update.')
                        return
                    self.handoff(c, room)
                    return
                if room.options & 2 and not c.get('native_capable'):
                    self.error(c, 'Update this client: native room settings are enabled.')
                    return
                if room.game == 0 and room.options & 1 and not c.get('hornet_capable'):
                    self.error(c, 'Update this client: Hornet is enabled in this room.')
                    return
                if room.game < 4 and room.options & 512 and not c.get('race_capable'):
                    self.error(c, 'Update this client: Real players only is enabled in this room.')
                    return
                if room.game < 4 and room.options & 256 and not c.get('race_capable'):
                    self.error(c, 'Update this client: collisions are disabled in this room.')
                    return
                if room.options and not c.get('options_capable'):
                    self.error(c, 'Update this client: Secret Cars is enabled in this room.')
                    return
                c['native_car'] = first_free_car(room)
                room.members.append(c)
            else:
                raise ValueError('invalid create/join command')
            c['room'] = room.id
            self.broadcast()
            if kind in (b'OLC3', b'OLC4') and room.host_mode in (2, 3):
                self.send(c, b'OHP1', struct.pack('<II16s', room.id, room.host_port, room.host_probe))
                self.post_room_chat(room, 0, 'System', 'Testing self-host TCP/UDP reachability and attempting UPnP. Please wait...')
                self.notify()
            if kind == b'OLJ1':
                self.replay_room_chat(c, room)
            if self.autostart and len(room.members) >= min(self.autostart, max_players_for_game(room.game)) and room.state == 0:
                self.authorize(room, f'autostart at {len(room.members)} cabinets')
            return
        if kind == b'OLT1' and len(body) == 4:
            room = self.rooms.get(struct.unpack('<I', body)[0])
            if room is None or room.members[0] is not c or c['room'] != room.id:
                self.error(c, 'Only this lobby\'s master can Start Link.')
                return
            max_players = max_players_for_game(room.game)
            if room.host_checking or room.state != 0 or not 2 <= len(room.members) <= max_players:
                self.error(c, f'Start Link needs 2-{max_players} joined cabinets in a waiting lobby.')
                return
            self.authorize(room, 'master authorized Start Link')
            return
        raise ValueError('unknown/malformed lobby command')

    async def control(self, reader, writer, body, host_capable=False, full_host_capable=False, room_host_capable=False):
        if len(body) != HELLO.size:
            raise ValueError('lobby hello size')
        version, mask, enabled, name = HELLO.unpack(body)
        if version != VERSION or enabled not in (0, 1, 2, 3, 6, 7, 14, 15, 22, 23, 32, 33, 34, 35, 38, 39, 46, 47, 54, 55) or mask & ~((1 << len(GAMES)) - 1) or len(self.clients) >= 64:
            raise ValueError('lobby version/limit/flags')
        c = dict(id=self.next_client, writer=writer, name=text(name, 'Player'), mask=mask, async_=bool(enabled & 1), room=0)
        c['options_capable'] = bool(enabled & 2)
        c['native_capable'] = bool(enabled & 4)
        c['hornet_capable'] = bool(enabled & 16)  # course-selection semantics; old car shortcut is not compatible
        c['race_capable'] = bool(enabled & 32)
        c['async'] = c.pop('async_')
        c['host_capable'] = host_capable
        c['full_host_capable'] = full_host_capable
        c['room_host_capable'] = room_host_capable
        c['ip'] = writer.get_extra_info('peername')[0]
        self.next_client += 1
        self.clients[c['id']] = c
        writer.write(frame(b'HEL1', b'Supermodel Online lobby v1'))
        self.snapshot(c)
        self.replay_lounge_chat(c)
        try:
            window, count = time.monotonic(), 0
            while True:
                kind, body, _ = await asyncio.wait_for(read_frame(reader), 30)
                now = time.monotonic()
                if now - window > 1:
                    window, count = now, 0
                count += 1
                if count > 24:
                    raise ValueError('lobby request rate exceeded')
                await self.command(c, kind, body)
                if writer.transport.get_write_buffer_size() > 32768:
                    await asyncio.wait_for(writer.drain(), 5)
        finally:
            self.clients.pop(c['id'], None)
            await self.leave(c)

    async def chat_session(self, reader, writer, body):
        if len(body) != CHAT_JOIN.size:
            await self.legacy.reject(writer, 2)
            return
        version, ticket, raw_name = CHAT_JOIN.unpack(body)
        room = self.tickets.get(ticket)
        if room is None or room.state not in (1, 2) or version != VERSION or ticket == bytes(16):
            await self.legacy.reject(writer, 2)
            return
        # A reconnect replaces the SAME authenticated cabinet, even in a full room.
        for old_writer, old_client in tuple(room.chat_clients.items()):
            if old_client['ticket'] == ticket:
                room.chat_clients.pop(old_writer, None)
                old_writer.close()
        if len(room.chat_clients) >= 16:
            await self.legacy.reject(writer, 1)
            return
        car, lobby_name = room.ticket_meta.get(ticket, (0, ''))
        assigned_name = lobby_name or chat_text(raw_name) or (f'Car {car}' if car else 'Player')
        cc = dict(writer=writer, car=car, name=assigned_name, ticket=ticket)
        room.chat_clients[writer] = cc
        writer.write(frame(b'HEL1', b'Supermodel Online chat v1') +
                     frame(b'RLC2', CHAT_WELCOME.pack(room.id, car, len(room.members), chat_fixed(assigned_name, 32), fixed(room.title, 64))))
        self.replay_room_chat(cc, room)
        if writer.transport.get_write_buffer_size() > 32768:
            await asyncio.wait_for(writer.drain(), 5)
        try:
            window, count = time.monotonic(), 0
            while True:
                kind, payload, _ = await asyncio.wait_for(read_frame(reader), 40)
                now = time.monotonic()
                if now - window > 1:
                    window, count = now, 0
                count += 1
                if count > 24:
                    raise ValueError('chat request rate exceeded')
                if kind == b'OLP1' and not payload:
                    self.send(cc, b'OLK1')
                elif kind == b'OLH4' and len(payload) == 4:
                    status, = struct.unpack('<I', payload)
                    if room.host_mode != 2 or car != 1 or status not in (1, 2, 3): raise ValueError('invalid host status')
                    room.host_last = now
                    if status == 3: self.host_failed(room, 'Self-host gameplay link closed.')
                    elif status == 2 and room.state == 1:
                        room.state = 2
                        self.broadcast()
                        self.notify()
                elif kind == b'OLR1' and len(payload) == 8:
                    rank, active = struct.unpack('<II', payload)
                    if active not in (0, 1) or (active and not 1 <= rank <= 40) or (not active and rank):
                        raise ValueError('invalid native standing')
                    if room.id not in self.rooms or room.state not in (1, 2):
                        raise ValueError('closed standings room')
                    # Ticket, NOT a sender-supplied cabinet or name, owns this row.
                    room.rank_updates[ticket] = (rank, now, writer)
                    rows = []
                    for rt, (rcar, rname) in sorted(room.ticket_meta.items(), key=lambda item: item[1][0]):
                        update = room.rank_updates.get(rt)
                        rrank = update[0] if update and now - update[1] <= 2.0 else 0
                        rows.append(struct.pack('<II32s', rcar, rrank, chat_fixed(rname, 32)))
                    # One response/request, not N broadcasts/request: O(N) bandwidth.
                    self.send(cc, b'OLB1', struct.pack('<I', len(rows)) + b''.join(rows))
                elif kind == b'OLS1' and len(payload) == CHAT_SEND.size:
                    if room.id in self.rooms and room.state in (1, 2):
                        self.post_room_chat(room, car, assigned_name, chat_text(payload))
                else:
                    raise ValueError('unknown/malformed chat command')
                if writer.transport.get_write_buffer_size() > 32768:
                    await asyncio.wait_for(writer.drain(), 5)
        finally:
            room.chat_clients.pop(writer, None)
            update = room.rank_updates.get(ticket)
            if update and update[2] is writer:
                room.rank_updates.pop(ticket, None)

    async def handle(self, reader, writer):
        task = asyncio.current_task()
        self.tasks.add(task)
        self.connections.add(writer)
        try:
            if len(self.connections) > 256:
                return
            tune_socket(writer)
            first = await asyncio.wait_for(read_frame(reader), 10)
            if first[0] != b'HEL1':
                raise ValueError('expected Hello')
            second = await asyncio.wait_for(read_frame(reader), 10)
            if second[0] == b'RLJ1':
                self.log('Direct legacy relay registration rejected: update Supermodel and use the Online GUI.')
                await self.legacy.reject(writer, 6)
            elif second[0] == b'ODA1':
                self.directory_advertisement(writer, second[1])
                await writer.drain()
            elif second[0] == b'OEH6':
                await self.control(reader, writer, second[1], second[0] != b'OLH1', second[0] in (b'OLH3', b'OEH6'), second[0] == b'OEH6')
            elif second[0] == b'RLC1':
                await self.chat_session(reader, writer, second[1])
            elif second[0] in (b'RLJ2', b'RLJ3') and len(second[1]) == JOIN2.size:
                version, channel, nonce, guid, ticket = JOIN2.unpack(second[1])
                room = self.tickets.get(ticket)
                if room is None or room.host_mode in (2, 3) or room.state != 1 or version != VERSION or channel not in (0, 1) or guid != GAMES[room.game][1] or nonce == bytes(16):
                    await self.legacy.reject(writer, 2)
                    return
                # Room nonce is the reserved ticket, never user-selected join order.
                body = JOIN.pack(version, channel, ticket, guid)
                registration = b'RLJ3' if second[0] == b'RLJ3' else b'RLJ1'
                await room.board.handle(reader, writer, (first, (registration, body, frame(registration, body))))
            else:
                raise ValueError('unknown registration; update relay/client together')
        except (asyncio.IncompleteReadError, ConnectionError, OSError, asyncio.TimeoutError, ValueError) as exc:
            self.log(f'ONLINE connection closed: {type(exc).__name__}')
        finally:
            writer.close()
            with contextlib.suppress(OSError, asyncio.TimeoutError):
                await asyncio.wait_for(writer.wait_closed(), 2)
            self.connections.discard(writer)
            self.tasks.discard(task)

    async def monitor(self):
        wakeup = self._wakeup
        while True:
            rooms = tuple(self.rooms.values())
            now = time.monotonic()
            deadlines = [r.deadline for r in rooms if r.state in (1, 3) and not (r.host_mode == 3 and r.directory_ready)]
            deadlines += [r.host_deadline for r in rooms if r.host_checking]
            if any(r.host_mode == 3 and r.directory_ready for r in rooms): deadlines.append(now + 1)
            if deadlines:
                timeout = max(0.005, min(1.0, min(deadlines) - now))
            elif any(r.state == 2 for r in rooms):
                timeout = 1.0
            else:
                timeout = None
            if wakeup is not None:
                if not wakeup.is_set():
                    if timeout is None:
                        await wakeup.wait()
                    else:
                        with contextlib.suppress(asyncio.TimeoutError):
                            await asyncio.wait_for(wakeup.wait(), timeout)
                wakeup.clear()
            else:
                await asyncio.sleep(0.05)
            for room in tuple(self.rooms.values()):
                if room.host_mode == 3 and room.directory_ready:
                    if room.state == 3 or time.monotonic() - room.host_last > 12:
                        await self.destroy(room, 'Directory listing expired.')
                    continue
                if room.state == 0 and room.host_checking and time.monotonic() >= room.host_deadline:
                    await self.host_fallback(room, 'host preparation timed out')
                if room.host_mode == 2 and room.state in (1, 2):
                    if (room.state == 1 and time.monotonic() >= room.deadline) or (room.state == 2 and time.monotonic() - room.host_last > 15):
                        self.host_failed(room, 'Self-host launch or management connection timed out.')
                    continue
                if room.state == 1:
                    if room.board.failed or len(room.board.players) != len(room.members) or time.monotonic() >= room.deadline:
                        room.board.fail('GUI launch failed/disconnected/timed out; create a new lobby and restart all clients')
                        room.state, room.deadline = 3, time.monotonic() + 15
                        self.broadcast()
                    elif all(len(channels) == 2 for channels in room.board.players.values()):
                        if await room.board.start():
                            room.state = 2
                        else:
                            room.state, room.deadline = 3, time.monotonic() + 15
                        self.broadcast()
                elif room.state == 2 and room.board.failed:
                    room.state, room.deadline = 3, time.monotonic() + 15
                    self.broadcast()
                elif room.state == 3 and time.monotonic() >= room.deadline:
                    await self.destroy(room, 'Session ended. Create a new lobby to play again.')

    async def close(self):
        if self.monitor_task:
            self.monitor_task.cancel()
            await asyncio.gather(self.monitor_task, return_exceptions=True)
        for writer in tuple(self.connections):
            writer.close()
        tasks = tuple(self.tasks)
        for task in tasks:
            task.cancel()
        await asyncio.gather(*tasks, return_exceptions=True)
        for room in tuple(self.rooms.values()):
            await self.destroy(room, 'Relay stopped.')

''',{name:globals()[name] for name in ('Room','frame','read_frame','tune_socket','JOIN','VERSION','DEFAULT_TICK_RATE')})

# Public namespaces for source tests/tools; compatibility names are in memory
# only. None of these modules is a separate file or relay launcher.
lobby=online_lobby
udp=relay_udp
sys.modules['relay_udp']=relay_udp
sys.modules['online_lobby']=online_lobby
sys.modules['online_lobby_eight_research']=online_lobby

if __name__ == '__main__':
    print('Rally split relay: 4-player and 8-player rooms are separate; update all clients and relay together.',flush=True)
    main()
