// SPDX-License-Identifier: GPL-3.0-or-later
// c++ -std=c++17 -O2 -ISrc Tests/IndyMotionTest.cpp -o /tmp/indy-motion-test
#include "Network/Games/Indy500/Motion.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

using namespace IndyMotion;

static void TestMapping()
{
  // Exercise 2–8 linked cabinets, subsets, all native owner ordinals, swapped
  // native rows (not lobby ordering), remote players and their CPU cars.
  for (unsigned seats=2;seats<=8;++seats)
    for (unsigned playing=2;playing<=seats;++playing)
      for (unsigned local=0;local<playing;++local)
        for (unsigned i=0;i<MaxActors;++i)
        {
          const unsigned owner=i%playing, row=seats-1-owner, slot=i/playing;
          const uint32_t source=GuestRx+row*RowBytes+slot*RecordBytes;
          const auto r=FindRecord(i,owner,playing,local,row,seats,source);
          assert(bool(r)==(owner!=local));
          if (r) assert(r.row==row && r.slot==slot);
          assert(!FindRecord(i,owner,playing,local,row,seats,source+4));
          assert(!FindRecord(i,owner,playing,local,row,seats,source+RowBytes));
          assert(!FindRecord(i,(owner+1)%playing,playing,local,row,seats,source));
        }
  assert(!FindRecord(0,0,1,0,0,8,GuestRx)); // solo
  assert(!FindRecord(1,1,2,0,0,9,GuestRx));
  assert(!FindRecord(1,1,2,0,8,8,GuestRx));
  assert(!FindRecord(16,0,2,1,0,8,GuestRx));
  assert(!FindRecord(0,0,2,1,0,8,0x1a12024)); // own TX, not RX
  std::puts("PASS native source / actor ownership (2–8, subset, local and wrong row rejected)");
}

static Sample At(uint32_t tick,Vec pos,uint16_t yaw=0)
{
  return {{pos,yaw},tick,2,0};
}

static void TestBounds()
{
  assert(YawDelta(2,65534)==4 && YawDelta(65534,2)==-4);
  assert(!ValidPosition({std::numeric_limits<double>::quiet_NaN(),0}) && !ValidPosition({0,-10000}));
  Predictor p;
  for (unsigned k=0;k<70;++k)
  {
    const Sample s=At(k,{k*1.85,0},uint16_t(65500+k*226));
    const Pose a=p.Step(s,k,100000,2);
    assert((a.p-s.pose.p).Length()<=MaxShift+1e-8);
    assert(std::abs(YawDelta(a.yaw,s.pose.yaw))<=MaxYawShift);
    const Pose repeated=p.Step(s,k,0,0);
    assert(repeated.p.x==a.p.x && repeated.yaw==a.yaw); // no double-stepping
  }
  assert(p.Step(At(70,{1000,200}),70,6,2).p.x==1000); // warp
  assert(p.Step(At(3,{1,2}),71,6,2).p.x==1); // reset / backwards tick
  assert(p.Step(At(3,{3,4}),72,6,2).p.x==3); // contradictory duplicate
  for (unsigned k=73;k<104;++k) p.Step(At(k,{1.9*k,0}),k,5,1);
  const auto hold=At(103,{1.9*103,0});
  assert(p.Step(hold,104,5,1).p.x>hold.pose.p.x);
  assert(p.Step(hold,114,5,1).p.x==hold.pose.p.x); // stale: return to native
  assert(p.Step(At(120,{20,2}),140,6,1).p.x==20); // render gap / state load
  for (unsigned k=141;k<170;++k) p.Step(At(k,{1.7*k,0}),k,4,1);
  auto other=At(170,{1.7*170,0}); other.peer=4;
  assert(p.Step(other,170,6,2).p.x==other.pose.p.x);
  auto moved=At(171,{1.7*171,0}); moved.peer=4; moved.slot=1;
  assert(p.Step(moved,171,6,2).p.x==moved.pose.p.x);
  assert(NearLocal({{4,0},0},{{14,0},1000},{0,0}).p.x==4);
  assert(NearLocal({{9,0},0},{{-3,0},1000},{0,0}).p.x==9);
  assert(NearLocal({{35,0},0},{{40,0},1000},{0,0}).p.x==40);
  const auto near=NearLocal({{15,0},65500},{{17,0},1000},{0,0});
  assert(near.p.x>15 && near.p.x<17 && YawDelta(near.yaw,65500)>0);
  p.Clear();
  for (unsigned k=0;k<40;++k)
  {
    const Sample s=At(UINT32_MAX-4+k,{k*1.7,0});
    auto a=p.Step(s,k,4,1);
    if (k>15) assert(a.p.x>s.pose.p.x+2);
  }
  std::puts("PASS bounded coast, shortest-arc yaw, stale/warp/peer resets, near-contact and tick wrap");
}

static double P95(std::vector<double> v)
{
  std::sort(v.begin(),v.end()); return v[(v.size()-1)*95/100];
}

static void TestCadence()
{
  // A stream trace with bounded burst/hold and curved, non-constant motion;
  // only the presentation clock is simulated here, not the native TCP ring.
  static const unsigned age[]={4,5,6,4,4,5,4,5,4,5,6,7,4,4,4,4,4,4,3,4};
  auto ground=[](unsigned n) {
    const double t=n*.013;
    return Pose{{120*std::sin(t),120*(1-std::cos(t))},uint16_t(std::lround(65536*t/6.28318530718))};
  };
  std::vector<double> nr,pr,ne,pe;
  Predictor p;
  Vec oldRaw{},oldPred{},rawV{},predV{};
  for (unsigned k=8;k<650;++k)
  {
    const unsigned tick=k-age[k%20];
    const Pose raw=ground(tick), actual=ground(k);
    const Pose shown=p.Step({raw,tick,2,0},k,4.0,1);
    if (k>40)
    {
      ne.push_back((raw.p-actual.p).Length()); pe.push_back((shown.p-actual.p).Length());
      nr.push_back(((raw.p-oldRaw)-rawV).Length()); pr.push_back(((shown.p-oldPred)-predV).Length());
    }
    rawV=raw.p-oldRaw; predV=shown.p-oldPred; oldRaw=raw.p; oldPred=shown.p;
  }
  const double rawErr=P95(ne), predErr=P95(pe), rawChg=P95(nr), predChg=P95(pr);
  assert(predErr<rawErr*0.65 && predChg<rawChg*0.55);
  std::printf("PASS curved burst/hold replay: p95 error %.3f -> %.3f; p95 step change %.3f -> %.3f\n",
              rawErr,predErr,rawChg,predChg);
}

int main()
{
  TestMapping(); TestBounds(); TestCadence();
}
