#!/usr/bin/env python3
"""ROM-free SGT capability isolation and ticketed room tests."""
import asyncio
import struct
from run_online_lobby import Client,board,online,relay,wait_for
from run_indy500_online import read
from run_indy_room_options import launch

async def test(count):
    legacy=relay.Room(log=lambda _:None)
    hub=online.Hub(legacy,log=lambda _:None)
    server=await asyncio.start_server(hub.handle,'127.0.0.1',0)
    port=server.sockets[0].getsockname()[1];hub.start()
    clients=[];sockets=[];g=online.SGT24H_GAME
    async def client(hello=online.SGT24H_HELLO,enabled=511):
        c=await Client().connect(port,hello=hello,enabled=enabled);clients.append(c);return c
    async def channel(ticket,ch,**kw):
        r,w=await board(port,g,ticket,ch,**kw);sockets.append(w);return r,w
    async def reject(ticket,**kw):
        r,w=await channel(ticket,0,**kw)
        assert (await read(r))[0]==b'HEL1';assert (await read(r))[0]==b'RLE1';w.close()
    try:
        master=await client();assert master.hello==online.SGT24H_RELAY_HELLO
        old=await client(b'Supermodel Online');missing=await client(enabled=1)
        for c in (old,missing):
            c.send(b'OLC1',online.CREATE.pack(g,bytes(64)));assert b'ROM' in await c.until(b'OLE1')
        room=await master.create(g)
        for c in (old,missing):
            c.send(b'OLQ1');assert await c.snapshot()==[]
            c.send(b'OLJ1',struct.pack('<I',room));assert b'ROM' in await c.until(b'OLE1')
        for options in (2,256,512,1024):
            master.send(b'ORO1',struct.pack('<II',room,options));assert b'valid room options' in await master.until(b'OLE1')
        master.send(b'OLT1',struct.pack('<I',room));assert b'2-4' in await master.until(b'OLE1')
        seats=[master]
        for i in range(1,count):
            c=await client();await c.join(room);seats.append(c)
        if count==4:
            extra=await client();extra.send(b'OLJ1',struct.pack('<I',room));assert b'full' in await extra.until(b'OLE1')
        seats[-1].send(b'OLT1',struct.pack('<I',room));assert b'Only' in await seats[-1].until(b'OLE1')
        master.send(b'OLT1',struct.pack('<I',room))
        raw=[await launch(c,b'OLG5') for c in seats]
        tickets=[]
        for i,p in enumerate(raw):
            a=online.LAUNCH.unpack(p[:online.LAUNCH.size]);assert a[:4]==(room,g,i,count)
            assert p[online.LAUNCH.size:]==bytes(8);tickets.append(a[4])
        assert len(set(tickets))==count
        await reject(bytes(16));await reject(tickets[0],bad_guid=relay.DAYTONA_GUID)
        await reject(tickets[0],registration=b'RLJ3')
        links={(i,ch):await channel(tickets[i],ch) for i in reversed(range(count)) for ch in (1,0)}
        for (i,ch),(r,w) in links.items():
            assert (await read(r))[0]==b'HEL1'
            k,p,_=await read(r);assert k==b'RLA1' and relay.READY.unpack(p)==(1,i,count,ch)
        for i in range(count):
            msg=relay.frame(b'NBD1',bytes([i+1])*relay.SGT24H_PAYLOAD)
            links[i,0][1].write(msg)
            assert (await read(links[(i-1)%count,1][0]))[2]==msg
        await reject(tickets[0])
        links[0,0][1].close()
        await wait_for(lambda:hub.rooms[room].state==3)
        print(f'PASS SGT ticketed lobby {count} seats: capability isolation, settings rejection, ownership, tickets, reserved order, routing, disconnect')
    finally:
        for c in clients:c.close()
        for w in sockets:w.close()
        server.close();await hub.close();await legacy.close();await server.wait_closed()
async def main():
    for n in (2,3,4):await test(n)
if __name__=='__main__':asyncio.run(main())
