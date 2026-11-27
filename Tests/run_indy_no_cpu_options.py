#!/usr/bin/env python3
"""ROM-free Indy players-only compatibility and frozen-launch test."""
import asyncio, struct
from run_online_lobby import Client, online, relay
from run_indy_room_options import await_view, launch

async def main():
    legacy=relay.Room(log=lambda _:None)
    hub=online.Hub(legacy,log=lambda _:None)
    server=await asyncio.start_server(hub.handle,'127.0.0.1',0); hub.start()
    port=server.sockets[0].getsockname()[1]; clients=[]
    async def connect(name,new):
        hello=b'Supermodel Online M2VIDEO2 1' if new else b'Supermodel Online M2VIDEO1 1'
        c=await Client().connect(port,name=name,enabled=511,hello=hello)
        expected=b'Supermodel Online lobby v1 M2VIDEO2 1' if new else b'Supermodel Online lobby v1 M2VIDEO1 1'
        assert c.hello==expected,(c.hello,expected)
        clients.append(c);return c
    try:
        master=await connect('master',True);guest=await connect('guest',True);old=await connect('old',False)
        room=await master.create(online.INDY500_GAME); await old.join(room)
        settings=online.INDY500_DEFAULT_OPTIONS; option=settings|512
        master.send(b'ORO1',struct.pack('<II',room,option))
        assert b'Update all room members' in await master.until(b'OLE1')
        old.send(b'OLL1')
        for _ in range(100):
            if len(hub.rooms[room].members)==1:break
            await asyncio.sleep(.01)
        assert len(hub.rooms[room].members)==1
        await guest.join(room)
        guest.send(b'ORO1',struct.pack('<II',room,option))
        assert b'Only the master' in await guest.until(b'OLE1')
        master.send(b'ORO1',struct.pack('<II',room,option))
        await asyncio.gather(*(await_view(c,room,(option,1)) for c in (master,guest)))
        old.send(b'OLQ1'); await await_view(old,room,(settings,1)) # browser view masks unknown bit
        old.send(b'OLJ1',struct.pack('<I',room))
        assert b'real players only' in await old.until(b'OLE1')
        master.send(b'OLT1',struct.pack('<I',room))
        for i,c in enumerate((master,guest)):
            body=await launch(c,b'OLG5')
            assert online.LAUNCH.unpack_from(body)[:4]==(room,online.INDY500_GAME,i,2)
            assert struct.unpack_from('<II',body,online.LAUNCH.size)==(option,0)
        master.send(b'ORO1',struct.pack('<II',room,settings))
        assert b'before launch' in await master.until(b'OLE1')
        print('PASS Indy players-only: versioned greeting, old-client mask/rejection, master-only change and frozen identical launch')
    finally:
        for c in clients:c.close()
        server.close();await hub.close();await legacy.close();await server.wait_closed()
if __name__=='__main__':asyncio.run(main())
