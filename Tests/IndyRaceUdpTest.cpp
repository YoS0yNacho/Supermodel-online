// ROM-free INDY native motion / ordered control framing checks.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Network/Games/Indy500/RaceUdp.h"
#include <cassert>
#include <cstdio>

using namespace IndyRaceUdp;

static void Float(uint8_t* dest,float f)
{
  uint32_t bits;
  std::memcpy(&bits,&f,4);
  Put(dest,bits,4);
}

static std::vector<uint8_t> Data(unsigned count,unsigned predecessor,uint32_t tick)
{
  std::vector<uint8_t> data(Indy500Online::PayloadSize);
  data[0]=uint8_t(predecessor);
  for (unsigned r=0;r<Indy500Online::Cabinets;++r)
  {
    auto* row=data.data()+1+r*IndyMotion::RowBytes;
    row[0]=uint8_t(1+(predecessor-1+r)%count);
    row[1]=0x42; row[RowState]=2;
    Put(row+IndyMotion::TickOffset,tick-r,4);
    for (unsigned s=0;s<Slots;++s)
    {
      auto* p=Record(row,s);
      Float(p,128.25f+s+r); Float(p+4,8.5f); Float(p+8,-308.75f+r);
      Put(p+12,0x6510,2); Put(p+14,0xfe28,2); Put(p+16,0xa40d,2);
      Put(p+StateOffset,s%2==0 ? 0xa001 : 0x8002,2);
      Put(p+20,0x12453357,4);
    }
    Put(row+0x19c,0x84653510,4); // unrelated bytes must remain exact
  }
  return data;
}

static Known At(uint32_t tick,float x,uint64_t time=0,uint16_t state=1)
{
  Motion m;
  Float(m.bytes.data(),x);
  m.state=state;
  return {m,tick,time};
}

static void TestVisibleMotion()
{
  constexpr uint64_t now=2'000'000;
  const auto older=At(21,14), fresh=At(24,19,now-16'000), newest=At(25,20.7f,now-5'000);
  assert(VisibleMotion(older,{},fresh,21,now).tick==24);
  assert(VisibleMotion(older,fresh,{},21,now).tick==24); // late full keyframe or early fallback
  assert(VisibleMotion(older,fresh,newest,21,now).tick==25);
  assert(VisibleMotion(older,newest,fresh,21,now).tick==25); // late UDP never unwinds motion
  assert(VisibleMotion(older,fresh,{},21,now+LeaveFreshUs).tick==21); // no unbounded stale hold
  const auto caught=At(24,19);
  assert(!VisibleMotion(caught,fresh,{},24,now).sampleUs); // clean return to ordered TCP
  const auto stateChange=At(21,14,0,2), warp=At(23,207), restart=At(1,-200);
  assert(VisibleMotion(stateChange,fresh,newest,21,now).motion.state==2);
  assert(VisibleMotion(warp,fresh,newest,23,now).motion.bytes==warp.motion.bytes);
  assert(VisibleMotion(restart,fresh,newest,1,now).tick==1);
  assert(VisibleMotion(older,At(62,14,now-16'000),{},21,now).tick==21);
  assert(VisibleMotion(older,At(24,19,now+100),{},21,now).tick==21);
  assert(VisibleMotion(At(UINT32_MAX,15),{},At(1,18,now-8'000),UINT32_MAX,now).tick==1);

  // Healthy LAN: a periodic complete TCP row is still transmitted with its
  // original tick/position. Locally it cannot rewind the more recent sample.
  Known visible;
  for (unsigned f=4;f<300;++f)
  {
    const uint64_t t=now+uint64_t(f*IndyMotion::FrameUs);
    const unsigned tick=f-(f%7==0 ? 1 : 0); // brief missed UDP arrival
    const unsigned old=f-3;
    const auto full=At(old,1.6f*old), head=At(tick,1.6f*tick,t-20000);
    const auto next=VisibleMotion(full,visible,f%29==0 ? Known{} : head,old,t);
    assert(next.tick>=visible.tick && f-next.tick<=2);
    assert(next.motion.bytes==At(next.tick,1.6f*next.tick).motion.bytes);
    assert(full.tick==old && !full.sampleUs); // no substitution of the forwarded anchor
    visible=next;
  }
  std::puts("PASS Indy remote motion does not rewind on full TCP keyframes, loss, stale UDP or tick wrap; real state/pose resets remain native");
}

int main()
{
  TestVisibleMotion();
  const Race race{4,42,3,1};
  auto raw=Data(2,1,UINT32_MAX);
  const auto sample=FromRow(race,raw.data()+1,1,2);
  assert(sample.mask==0x55 && sample.tick==UINT32_MAX && sample.peer==1);
  auto statusOnly=FromRow(race,raw.data()+1,1,2,0);
  const auto beat=Encode(statusOnly);
  Sample decodedBeat;
  assert(!statusOnly.mask && Decode(beat.data(),beat.size(),race,1,decodedBeat));
  assert(!Matches(decodedBeat,0,0xa001,UINT32_MAX));
  assert(FromRow(race,raw.data()+1,1,2,2).mask==1);
  assert(!SameMotion(sample,statusOnly) && SameMotion(sample,sample));
  auto wire=Encode(sample);
  Sample decoded;
  assert(Decode(wire.data(),wire.size(),race,1,decoded));
  assert(decoded.cars[2].bytes==sample.cars[2].bytes && decoded.cars[2].state==0xa001);
  assert(!Decode(wire.data(),wire.size(),race,2,decoded));
  assert(!Decode(wire.data(),wire.size()-1,race,1,decoded));
  assert(!Decode(wire.data(),wire.size(),Race{4,41,3,1},1,decoded));
  assert(!Decode(wire.data(),wire.size(),Race{4,42,3,2},1,decoded));
  assert(Matches(decoded,2,0xa001,UINT32_MAX-1));
  assert(Matches(decoded,2,0xa001,0)); // small lag across tick wrap
  assert(!Matches(decoded,2,0xa000,UINT32_MAX-1));
  assert(!Matches(decoded,1,0x8002,UINT32_MAX-1));
  assert(!Matches(decoded,2,0xa001,137));
  Known known{decoded.cars[2],UINT32_MAX,0};
  decoded.tick=0;
  assert(Continuous(known,decoded,2));
  Float(decoded.cars[2].bytes.data(),900.0f);
  assert(!Continuous(known,decoded,2));
  wire[20+MotionBytes]=4;
  assert(!Decode(wire.data(),wire.size(),race,1,decoded));
  wire=Encode(sample);
  Put(wire.data()+20,0x7fc00000,4); // NaN
  assert(!Decode(wire.data(),wire.size(),race,1,decoded));

  for (unsigned count=2;count<=8;++count) for (unsigned predecessor=1;predecessor<=count;++predecessor)
  {
    raw=Data(count,predecessor,1200);
    Holes stripped{0x55,0x10,0,0x44,0,0,1,0x04};
    auto packed=Pack(raw.data(),stripped,1);
    std::vector<uint8_t> packet;
    Holes masks;
    uint8_t sync=0;
    assert(packed.size()<Indy500Online::PayloadSize);
    assert(Unpack(packed,count,predecessor,packet,masks,sync));
    assert(sync==1 && masks==stripped && packet.size()==raw.size());
    for (unsigned r=0;r<masks.size();++r) for (unsigned s=0;s<Slots;++s)
      if (masks[r]&(1u<<s))
      {
        auto* p=Record(packet.data()+1+r*IndyMotion::RowBytes,s);
        assert(std::all_of(p,p+MotionBytes,[](uint8_t n){return n==0;}));
        std::copy_n(Record(raw.data()+1+r*IndyMotion::RowBytes,s),MotionBytes,p);
      }
    assert(packet==raw); // includes rows' control fields and unused tail
    auto bad=packed;bad[8]=2;
    assert(!Unpack(bad,count,predecessor,packet,masks,sync));
    bad=packed;bad[0]^=2; // cannot omit movement from this special-state slot
    assert(!Unpack(bad,count,predecessor,packet,masks,sync));
    bad=packed;bad[1]|=1; // a normal slot whose nonzero data is not omitted
    assert(!Unpack(bad,count,predecessor,packet,masks,sync));
    bad=packed;bad.push_back(0);
    assert(!Unpack(bad,count,predecessor,packet,masks,sync));
    assert(!Unpack(packed,count,predecessor==1?count:1,packet,masks,sync));
    std::vector<uint8_t> txBase,rxBase;
    packed=Pack(raw.data(),stripped,1,&txBase);
    assert(Unpack(packed,count,predecessor,packet,masks,sync,&rxBase));
    auto changed=raw;
    changed[2]^=0x80; // a reliable control change
    const auto delta=Pack(changed.data(),stripped,1,&txBase);
    assert(delta.size()<packed.size()/3 && delta[8]==0x81);
    assert(!Unpack(delta,count,predecessor,packet,masks,sync));
    assert(Unpack(delta,count,predecessor,packet,masks,sync,&rxBase));
    assert(txBase==rxBase && packet==rxBase && packet[2]==changed[2]);
    raw[1+IndyMotion::RowBytes]=0;
    bad=Pack(raw.data(),stripped,1);
    assert(!Unpack(bad,count,predecessor,packet,masks,sync));
  }
  std::puts("PASS Indy race UDP codec, wrap/epoch/ownership/state/warp guards, bounded compressed control pairs and exact reliable bytes (2-8)");
}
