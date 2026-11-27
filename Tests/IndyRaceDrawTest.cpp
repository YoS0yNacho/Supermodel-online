// SPDX-License-Identifier: GPL-3.0-or-later
// Pure checks for the render-only race UDP selection; no ROM required.
#include "Network/Games/Indy500/RaceUdp.h"
#include <cassert>
#include <cstdio>
#include <limits>
using namespace IndyRaceUdp;
static void F(uint8_t* p,float f) { uint32_t bits; std::memcpy(&bits,&f,4); Put(p,bits,4); }
static Motion M(float x,uint16_t state=1) {
  Motion m; F(m.bytes.data(),x); F(m.bytes.data()+4,0); F(m.bytes.data()+8,0); m.state=state; return m;
}
int main() {
  const Race race{4,42,3,1};
  const IndyMotion::Sample native{{{10,0},0},100,2,0};
  Sample s{race,104,2,1}; s.cars[0]=M(14);
  constexpr uint64_t now=2000000;
  auto pick=[&](const IndyMotion::Sample& n,uint16_t st,Race r,const Sample& x,uint64_t us,uint64_t t) {
    return PickDraw(n,st,r,x,us,t);
  };
  auto got=pick(native,1,race,s,now-200000,now);
  assert(got && got.pose.tick==104 && got.pose.pose.p.x==14 && got.age==200000 && got.gap==4);
  assert(pick(native,1,race,s,now-LeaveFreshUs,now));
  assert(!pick(native,1,race,s,now-LeaveFreshUs-1,now));
  assert(!pick(native,1,race,s,0,now));
  assert(!pick(native,1,race,s,now+1,now));
  assert(!pick(native,2,race,s,now-200000,now));
  assert(!pick(native,1,Race{5,42,3,1},s,now-200000,now));
  assert(!pick(native,1,Race{4,43,3,1},s,now-200000,now));
  assert(!pick(native,1,Race{4,42,7,1},s,now-200000,now));
  assert(!pick(native,1,Race{4,42,3,2},s,now-200000,now));
  auto altered=s; altered.peer=1; assert(!pick(native,1,race,altered,now-200000,now));
  altered=s; altered.mask=0; assert(!pick(native,1,race,altered,now-200000,now));
  altered=s; altered.tick=100; assert(!pick(native,1,race,altered,now-200000,now));
  altered=s; altered.tick=99; assert(!pick(native,1,race,altered,now-200000,now));
  altered=s; altered.tick=100+IndyPoseUdp::MaxNewerTicks+1; assert(!pick(native,1,race,altered,now-200000,now));
  altered=s; altered.cars[0]=M(1000); assert(!pick(native,1,race,altered,now-200000,now));
  altered=s; altered.cars[0]=M(14,2); assert(!pick(native,2,race,altered,now-200000,now));
  altered=s; Put(altered.cars[0].bytes.data()+14,10000,2); assert(!pick(native,1,race,altered,now-200000,now));
  auto wrongSlot=native; wrongSlot.slot=Slots; assert(!pick(wrongSlot,1,race,s,now-200000,now));
  auto wrongPose=native; wrongPose.pose.p.x=std::numeric_limits<double>::quiet_NaN();
  assert(!pick(wrongPose,1,race,s,now-200000,now));
  auto wrap=native; wrap.tick=UINT32_MAX-2; altered=s; altered.tick=1;
  assert(pick(wrap,1,race,altered,now-200000,now));
  IndyPoseUdp::Blend eased, immediate;
  const IndyMotion::Sample still{{{0,0},0},10,2,0};
  const IndyMotion::Pose target{{8,0},0};
  const auto e1=eased.Step(still,still.pose,&target,1,true);
  const auto i1=immediate.Step(still,still.pose,&target,1);
  assert(e1.p.x>0 && e1.p.x<i1.p.x); // ease the first correction
  for (unsigned f=2;f<=4;++f) {
    const auto e=eased.Step(still,still.pose,&target,f,true);
    assert(e.p.x>=0 && e.p.x<=8 && (e.p-still.pose.p).Length()<=IndyMotion::MaxShift);
  }
  for (unsigned f=5;f<=8;++f) eased.Step(still,still.pose,nullptr,f,true);
  assert(eased.Weight()==0 && eased.Step(still,still.pose,nullptr,9,true).p.x==0);
  const auto reacquire=eased.Step(still,still.pose,&target,10,true);
  assert(std::abs(reacquire.p.x-e1.p.x)<1e-9); // no old offset on reacquisition
  std::puts("PASS draw-only race UDP age/identity/state/tick/warp guards, wrap and bounded eased fallback");
}
