// SPDX-License-Identifier: GPL-3.0-or-later
#include "Network/Games/Indy500/UdpPose.h"
#include "Network/RelayUdpWire.h"
#include <cassert>
#include <cstdio>
#include <limits>

int main()
{
  using namespace IndyPoseUdp;
  static_assert(Bytes{}.size()+RelayUdp::HeaderSize+RelayUdp::TrailerSize+1 < RelayUdp::DatagramSize);
  const Race race{2,4,0x83,1};
  Packet p{race,1,8,0x81};
  p.poses[0]={{10.5,-19.25},11};
  p.poses[7]={{16,110},32};
  const auto wire=Encode(p);
  Packet decoded;
  assert(Decode(wire.data(),wire.size(),race,8,decoded));
  assert(decoded.poses[0].p.x==10.5 && decoded.poses[0].p.z==-19.25 && decoded.poses[7].yaw==32);
  assert(!Decode(wire.data(),wire.size(),race,7,decoded));
  assert(!Decode(wire.data(),wire.size(),{2,3,0x83,1},8,decoded));
  assert(!Decode(wire.data(),wire.size(),{1,4,0x83,1},8,decoded));
  assert(!Decode(wire.data(),wire.size(),{2,4,0x83,0},8,decoded));
  assert(!Decode(wire.data(),wire.size(),{2,4,0xc3,1},8,decoded));
  assert(!Decode(wire.data(),wire.size()-1,race,8,decoded));
  auto bad=wire; bad[30]=1;
  assert(!Decode(bad.data(),bad.size(),race,8,decoded));
  p.poses[7].p.x=std::numeric_limits<double>::infinity(); bad=Encode(p);
  assert(!Decode(bad.data(),bad.size(),race,8,decoded));
  std::puts("PASS Indy pose packet: one datagram, frozen race/seat/slot identity, exact sizes and finite poses");

  Sample tcp{{{8,-19.25},65500},0xfffffffe,8,0};
  Received rx{decoded,10000,50000,25000,true};
  auto pick=Pick(tcp,rx,60000);
  assert(pick && pick.gap==3 && pick.age==35000 && pick.pose.tick==1);
  assert(!Pick(tcp,rx,49000));
  assert(!Pick(tcp,rx,50000+MaxAgeUs-25000+1));
  rx.timed=false; assert(!Pick(tcp,rx,60000));
  rx.timed=true; rx.packet.tick=tcp.tick; assert(!Pick(tcp,rx,60000));
  rx.packet.tick=tcp.tick-1; assert(!Pick(tcp,rx,60000));
  rx.packet.tick=tcp.tick+MaxNewerTicks+1; assert(!Pick(tcp,rx,60000));
  rx.packet.tick=1; rx.packet.poses[0].p.x=100; assert(!Pick(tcp,rx,60000));
  rx.packet.poses[0].p.x=10.5; tcp.slot=1; assert(!Pick(tcp,rx,60000));
  std::puts("PASS Indy pose selection: wrap, freshest strictly newer tick, timing expiry, owner and warp checks");

  Blend mix;
  Sample at{{{0,0},0},5,2,0};
  const Pose base{{2,0},80}, ahead{{22,0},4000};
  for (unsigned f=1;f<=4;++f)
  {
    const auto view=mix.Step(at,base,&ahead,f);
    assert(view.p.x==2+2.5*f && mix.Weight()==f);
    assert(IndyMotion::YawDelta(view.yaw,at.pose.yaw)<=1536);
    assert(mix.Step(at,base,&ahead,f).p.x==view.p.x);
  }
  for (unsigned f=5;f<=8;++f)
  {
    assert(mix.Step(at,base,nullptr,f).p.x==2+2.5*(8-f));
    assert(mix.Weight()==8-f);
  }
  mix.Step(at,base,&ahead,9); at.pose.p.x=100;
  assert(mix.Step(at,at.pose,nullptr,10).p.x==100 && !mix.Weight());
  assert(IndyMotion::NearLocal(at.pose,{{112,0},1000},{101,0}).p.x==100);
  std::puts("PASS Indy UDP to TCP fallback: one blend step per frame, bounded offset, fade, warp and near-local reset");
}
