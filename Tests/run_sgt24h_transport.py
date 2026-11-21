#!/usr/bin/env python3
"""ROM-free SGT native transport tests; NOT lobby or emulator integration."""
import asyncio
from run_online_lobby import relay, wait_for
from run_indy500_online import StreamTransport, read

async def test(count, fallback, latency):
    room=relay.Room(log=lambda _:None,latency_ms=latency,jitter_ms=latency//4)
    sockets=[]
    async def handler(r,w):
        if fallback:w._transport=StreamTransport(w.transport)
        await room.handle(r,w)
    server=await asyncio.start_server(handler,'127.0.0.1',0)
    port=server.sockets[0].getsockname()[1]
    async def connect(seat,ch,kind=b'RLJ1',guid=relay.SGT24H_GUID):
        r,w=await asyncio.open_connection('127.0.0.1',port);sockets.append(w)
        w.write(relay.frame(b'HEL1',b'SGT native test')+relay.frame(kind,relay.JOIN.pack(1,ch,seat.to_bytes(16,'little'),guid)))
        await w.drain();return r,w
    async def reject(seat,ch=0,**kw):
        r,w=await connect(seat,ch,**kw)
        assert (await read(r))[0]==b'HEL1'
        assert (await read(r))[0]==b'RLE1'
        w.close()
    try:
        await reject(1,kind=b'RLJ3')
        await reject(0)
        links={}
        for i in range(count):
            for ch in (1,0):links[i,ch]=await connect(i+1,ch)
        await wait_for(lambda:len(room.players)==count and all(len(c)==2 for c in room.players.values()))
        await reject(1) # duplicate channel
        await reject(99,guid=relay.DAYTONA_GUID)
        if count==4:await reject(5)
        assert await room.start()
        for (i,ch),(r,w) in links.items():
            assert (await read(r))[0]==b'HEL1'
            kind,payload,_=await read(r)
            assert kind==b'RLA1' and relay.READY.unpack(payload)==(1,i,count,ch)
        assert not room.udp_sessions and not room.star_active and room.adaptive_ctl is None
        async def send(i):
            dest=(i-1)%count
            frames=[relay.frame(b'NBD1',bytes([tag,n])+bytes([i+1])*(relay.SGT24H_PAYLOAD-2)) for n,tag in enumerate((0xff,0xfe,i+1,0xfc,i+1))]
            w=links[i,0][1]
            w.write(frames[0][:5]);await w.drain();await asyncio.sleep(.01)
            w.write(frames[0][5:]+b''.join(frames[1:]));await w.drain()
            for frame in frames:assert (await read(links[dest,1][0]))[2]==frame
        await asyncio.gather(*(send(i) for i in range(count)))
        await reject(99) # running room cannot acquire a new cabinet
        links[0,0][1].write(relay.frame(b'NBD1',bytes(relay.SGT24H_PAYLOAD-1)))
        await links[0,0][1].drain()
        await wait_for(lambda:room.failed)
        for key,(r,w) in links.items():
            if key!=(0,0):assert (await read(r))[0]==b'RLE1'
        print(f'PASS SGT transport: {count} cabinets, fallback={fallback}, latency={latency}ms; admission, reverse ring, exact bytes, fatal malformed frame')
    finally:
        for w in sockets:w.close()
        server.close();await server.wait_closed();await room.close()

async def main():
    for n in (2,3,4):
        for fallback,latency in ((False,0),(False,35),(True,35)):
            await test(n,fallback,latency)
if __name__=='__main__':asyncio.run(main())
