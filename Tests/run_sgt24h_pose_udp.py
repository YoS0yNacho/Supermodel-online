#!/usr/bin/env python3
"""SGT optional UDP negotiation/relay routing. Not a semantic pose-decoder test."""
import asyncio,struct,time
from run_online_lobby import Client,board,online,relay
from run_indy_room_options import launch
from run_indy500_online import read
from run_indy_pose_udp import Inbox
async def check(count,udp=True,mixed=False):
 loop=asyncio.get_running_loop();legacy=relay.Room(log=lambda _:None)
 if udp:_,legacy.udp=await loop.create_datagram_endpoint(relay.udp.UdpRelay,local_addr=('127.0.0.1',0))
 hub=online.Hub(legacy,log=lambda _:None);server=await asyncio.start_server(hub.handle,'127.0.0.1',0);hub.start()
 port=server.sockets[0].getsockname()[1];clients=[];links={};poses={}
 try:
  for i in range(count):clients.append(await Client().connect(port,hello=online.SGT24H_HELLO,enabled=511))
  room=await clients[0].create(online.SGT24H_GAME)
  for c in clients[1:]:await c.join(room)
  clients[0].send(b'OLT1',struct.pack('<I',room))
  tickets=[online.LAUNCH.unpack_from(await launch(c,b'OLG5'))[4] for c in clients]
  for i in range(count):
   hello=b'older SGT client' if mixed and i==count-1 else relay.SGT24H_POSE_HELLO
   for ch in (0,1):links[i,ch]=await board(port,online.SGT24H_GAME,tickets[i],ch,hello=hello)
  active=udp and not mixed
  for (i,ch),(r,w) in links.items():
   assert (await read(r))[0]==b'HEL1'
   kind,payload,_=await read(r)
   if active and ch==0:
    assert kind==b'RLU1';v,up,index,n,token=struct.unpack('<IIII16s',payload);assert (v,index,n)==(1,i,count)
    t,inbox=await loop.create_datagram_endpoint(Inbox,remote_addr=('127.0.0.1',up));poses[i]=t,inbox,token
    t.sendto(relay.udp.HEADER.pack(b'UGP1',relay.udp.WIRE_VERSION,relay.udp.HELLO,0,0,token,1,i,0,time.monotonic_ns()//1000))
    assert relay.udp.parse(await inbox.receive())[2]==relay.udp.ACK
    kind,payload,_=await read(r)
   assert kind==b'RLA1' and relay.READY.unpack(payload)==(1,i,count,ch)
  if active:
   payload=b'\0'+bytes(relay.SGT24H_POSE_WIRE)+bytes(40)
   t,_,token=poses[0]
   t.sendto(relay.udp.HEADER.pack(b'UGP1',relay.udp.WIRE_VERSION,relay.udp.SNAPSHOT,0,1,token,2,0,len(payload),time.monotonic_ns()//1000)+payload)
   for i in range(1,count):
    packet=await poses[i][1].receive();h=relay.udp.parse(packet)
    assert h[5]==poses[i][2] and h[7]==0 and packet[relay.udp.HEADER.size:]==payload
  for i in range(count):
   payload=bytes([i+1])+bytes(relay.SGT24H_PAYLOAD-1)
   links[i,0][1].write(relay.frame(b'NBD1',payload));await links[i,0][1].drain()
   assert (await read(links[(i-1)%count,1][0]))[1]==payload
  print(f'PASS SGT {count}: UDP={active}, mixed={mixed}, native TCP ring preserved')
 finally:
  for t,_,_ in poses.values():t.close()
  for _,w in links.values():w.close()
  for c in clients:c.close()
  server.close();await server.wait_closed();await hub.close();await legacy.close()
  if udp:legacy.udp.transport.close()
async def main():
 for n in (2,3,4):
  await check(n);await check(n,mixed=True);await check(n,udp=False)
if __name__=='__main__':asyncio.run(main())
