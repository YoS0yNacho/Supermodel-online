// SPDX-License-Identifier: GPL-3.0-or-later
#include "Network/Games/Srallyc/Motion.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <limits>

using namespace SrallycMotion;

static Row TestRow(unsigned seat,double x, double z=42, double vx=0.55, unsigned race=940)
{
  std::array<uint8_t,HeaderBytes> b{};
  Put(b.data(),seat,2); Put(b.data()+2,seat,2); Put(b.data()+0x18,6,4); Put(b.data()+0x1c,race,4);
  Put(b.data()+0x24,1,4);
  Put(b.data()+0x28,FloatBits(float(x)),4); Put(b.data()+0x30,FloatBits(float(z)),4);
  Put(b.data()+0x34,FloatBits(float(vx)),4); Put(b.data()+0x44,FloatBits(.12f),4);
  assert(!DecodeRow(b.data(),b.size(),seat==1 ? 2 : 1));
  assert(!DecodeRow(b.data(),b.size()-1,seat));
  auto r=DecodeRow(b.data(),b.size(),seat);
  b[0x18]=5;
  assert(!DecodeRow(b.data(),b.size(),seat));
  return r;
}

int main()
{
  const Row native=TestRow(3,51), local=TestRow(1,100,96), near=TestRow(1,53,42,0);
  assert(native && native.scene.part==1 && native.pose.p.x==51 && native.seat==3);
  assert(!TestRow(3,std::numeric_limits<double>::infinity()) && !TestRow(3,51,42,NAN));
  assert(!TestRow(5,51) && !TestRow(3,51,42,2.6) && !TestRow(3,51,42,0.55,0));
  Publisher pub;
  assert(pub.Push(native) && !pub.Push(native));
  assert(pub.Push(TestRow(3,51.55)));
  const auto* next=pub.Push(TestRow(3,52.1));
  assert(next && next->n==3 && next->anchors[2]==native.key);
  auto wire=Encode(*next);
  Received r;
  assert(Decode(wire.data(),wire.size(),3,r.p));
  assert(!Decode(wire.data(),wire.size(),4,r.p));
  assert(!Decode(wire.data(),wire.size()-1,3,r.p));
  auto bad=wire;bad[44]=1;
  assert(!Decode(bad.data(),bad.size(),3,r.p));
  bad=wire;bad[47]=1;
  assert(!Decode(bad.data(),bad.size(),3,r.p));
  bad=wire;Put(bad.data()+24,FloatBits(INFINITY),4);
  assert(!Decode(bad.data(),bad.size(),3,r.p));
  bad=wire;Put(bad.data()+48+8*7,123,8);
  assert(!Decode(bad.data(),bad.size(),3,r.p));
  r.stamp=29;r.received=1000000;r.age=32000;
  const auto fresh=Latest(native,r,1020000);
  assert(fresh && fresh.gap==2 && fresh.age==52000);
  assert(!Latest(native,r,999999) && !Latest(native,r,1200000));
  assert(!Latest(TestRow(3,52.1),r,1020000)); // exact same or newer TCP: no donor
  assert(!Latest(TestRow(3,52.65),r,1020000));
  assert(!Latest(TestRow(3,51,42,0.55,941),r,1020000));
  assert(!Latest(TestRow(2,51),r,1020000));
  auto off=r;off.p.row.pose.p={80,80};assert(!Latest(native,off,1020000));
  const auto* tele=pub.Push(TestRow(3,600));
  assert(tele && tele->n==1 && tele->seq==1); // old anchors revoked by a jump
  assert(!pub.Push({}));
  for (unsigned i=0;i<90;++i) assert(pub.Push(TestRow(3,50+0.55*i)));
  const auto* capped=pub.Push(TestRow(3,99.5));assert(capped && capped->n==HistorySize && capped->seq==91);

  Predictor pred;
  const auto first=TestRow(3,50.45);
  auto d=pred.Step(first,local,{},1,1000000);
  assert(d.shown.p.x==first.pose.p.x);
  d=pred.Step(native,local,r,2,1020000);
  assert(d.weight==1 && d.udp && d.shown.p.x>d.tcp.p.x);
  for (unsigned f=3;f<=5;++f)
    assert(pred.Step(native,local,r,f,1010000+17000*f).weight==f-1);
  for (unsigned f=6;f<=9;++f)
  {
    d=pred.Step(native,local,{},f,1300000);
    assert(!d.udp && d.weight==9-f);
    assert((d.shown.p-native.pose.p).Length()<=MaxShift);
  }
  assert(d.tcp.p.x==d.shown.p.x);
  assert(pred.Step(native,near,{},10,1310000).shown.p.x==native.pose.p.x);
  const auto jump=TestRow(3,-300);
  assert(pred.Step(jump,local,r,11,1320000).shown.p.x==jump.pose.p.x);
  assert(pred.Step(TestRow(3,-299.45,42,0.55,7),local,r,12,1340000).weight==0);
  const Pose close1=LimitContact({{0,0},0},{{3,0},0.1},7);
  const Pose close2=LimitContact({{0,0},0},{{6,0},0.15},7);
  assert(close1.p.x==close2.p.x && close1.p.x<=0.8 && 7-close2.p.x>ContactRadius);
  assert(close1.yaw==close2.yaw && close1.yaw<=0.022);
  assert(LimitContact({{0,0},0},{{3,0},0.1},ContactRadius).p.x==0);
  assert(LimitContact({{0,0},0},{{4,0},0.1},24).p.x==4);
  const Pose rotation=Bound({{0,0},3.13},{{12,0},-3.12});
  assert(rotation.p.x==MaxShift && std::abs(rotation.yaw-3.13)<0.04);

  // One update every three frames with fixed motion: prediction should reduce
  // visible step discontinuities without changing the authoritative rows.
  Predictor tcp;
  double prevRaw=0, prevDraw=0, prevRawStep=0, prevDrawStep=0, rawJerk=0, drawnJerk=0;
  for (unsigned f=1;f<=150;++f)
  {
    const auto a=TestRow(3,10+0.55*(3*(f/3)));
    const auto l=TestRow(1,80,-200);
    auto sample=tcp.Step(a,l,{},f,20000*f);
    assert(!sample.udp && (sample.shown.p-a.pose.p).Length()<=MaxShift);
    const double rawStep=a.pose.p.x-prevRaw, drawStep=sample.shown.p.x-prevDraw;
    if (f>20) { rawJerk+=std::abs(rawStep-prevRawStep); drawnJerk+=std::abs(drawStep-prevDrawStep); }
    prevRaw=a.pose.p.x;prevDraw=sample.shown.p.x;prevRawStep=rawStep;prevDrawStep=drawStep;
  }
  assert(drawnJerk<rawJerk*0.7);
  std::printf("PASS Rev C row/pose codec, scene/seat guards, monotonic anchors, age, reset, contact and fallback; TCP jerk %.3f -> %.3f\n",rawJerk,drawnJerk);
}
