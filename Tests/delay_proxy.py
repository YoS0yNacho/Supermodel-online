#!/usr/bin/env python3
"""Userspace TCP delay proxy: adds real one-way latency to a byte stream.

Sits between clients and the relay server. Each direction's chunks are
delayed by delay_ms +/- jitter_ms, emulating a real network path without
root (no tc/netem needed). Byte-stream delay is more realistic than the
relay's frame-level Latency: it delays the wire bytes, not framed frames.
"""
import asyncio
import contextlib
import random
import sys


async def pipe(reader, writer, delay_ms, jitter_ms):
    try:
        while True:
            data = await reader.read(65536)
            if not data:
                break
            d = delay_ms
            if jitter_ms:
                d += random.randint(-jitter_ms, jitter_ms)
            await asyncio.sleep(max(0, d) / 1000.0)
            writer.write(data)
            await writer.drain()
    except (ConnectionError, asyncio.CancelledError, OSError):
        pass
    finally:
        with contextlib.suppress(Exception):
            writer.close()


async def handle(reader, writer, upstream_host, upstream_port, delay_ms, jitter_ms):
    up_r, up_w = await asyncio.open_connection(upstream_host, upstream_port)
    t1 = asyncio.create_task(pipe(reader, up_w, delay_ms, jitter_ms))
    t2 = asyncio.create_task(pipe(up_r, writer, delay_ms, jitter_ms))
    try:
        await asyncio.gather(t1, t2)
    finally:
        t1.cancel()
        t2.cancel()
        with contextlib.suppress(Exception):
            up_w.close()
        with contextlib.suppress(Exception):
            writer.close()


async def main():
    upstream_host = sys.argv[1]
    upstream_port = int(sys.argv[2])
    listen_port = int(sys.argv[3])
    delay_ms = int(sys.argv[4]) if len(sys.argv) > 4 else 200
    jitter_ms = int(sys.argv[5]) if len(sys.argv) > 5 else 8
    server = await asyncio.start_server(
        lambda r, w: handle(r, w, upstream_host, upstream_port, delay_ms, jitter_ms),
        '127.0.0.1', listen_port)
    print(f'DELAY_PROXY listening on 127.0.0.1:{listen_port} -> {upstream_host}:{upstream_port} '
          f'delay={delay_ms}ms jitter={jitter_ms}ms', flush=True)
    async with server:
        await server.serve_forever()


asyncio.run(main())
