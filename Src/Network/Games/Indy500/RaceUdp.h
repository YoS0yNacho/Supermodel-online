// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_INDY500_RACE_UDP_H
#define INCLUDED_INDY500_RACE_UDP_H

#include "UdpPose.h"
#include "Network/RelayUdpWire.h"
#include <algorithm>
#include <array>
#include <vector>

namespace IndyRaceUdp {
using IndyPoseUdp::Race;
using IndyPoseUdp::Get;
using IndyPoseUdp::Put;
constexpr unsigned Slots=Indy500Online::PoseSlots;
constexpr unsigned MotionBytes=18; // native X/Y/Z floats and three signed angles
constexpr unsigned StateOffset=18; // ab340 loads this separately: never write it from UDP
constexpr unsigned RowState=0x19;
constexpr unsigned MaxTickAhead=40, MaxTickBehind=12;
constexpr uint64_t EnterFreshUs=155000, LeaveFreshUs=340000, EnterStableUs=1000000;
constexpr uint64_t KeyframeUs=500000;
using Wire=std::array<uint8_t,Indy500Online::RaceWireBytes>;
using Holes=std::array<uint8_t,Indy500Online::Cabinets>;
static_assert(sizeof(Wire)==20+Slots*(MotionBytes+2));
static_assert(sizeof(Holes)+1==Indy500Online::RacePairHeaderBytes);
static_assert(IndyMotion::RowBytes*Indy500Online::Cabinets+1==Indy500Online::PayloadSize);

inline const uint8_t* Record(const uint8_t* row,unsigned slot)
{
  return row+IndyMotion::PayloadOffset+slot*IndyMotion::RecordBytes;
}
inline uint8_t* Record(uint8_t* row,unsigned slot)
{
  return row+IndyMotion::PayloadOffset+slot*IndyMotion::RecordBytes;
}
inline bool RacingRow(const uint8_t* row,unsigned count)
{
  return row[0] && row[0]<=count && (row[RowState]&2);
}
struct Motion {
  std::array<uint8_t,MotionBytes> bytes{};
  uint16_t state=0;
  IndyMotion::Pose Pose() const
  {
    return {{IndyPoseUdp::GetPosition(bytes.data()),IndyPoseUdp::GetPosition(bytes.data()+8)},
            uint16_t(Get(bytes.data()+14,2))};
  }
  explicit operator bool() const
  {
    return (state&15)==1 && IndyMotion::ValidPosition(Pose().p) &&
           std::abs(IndyPoseUdp::GetPosition(bytes.data()+4))<1024;
  }
};
inline Motion ReadMotion(const uint8_t* p)
{
  Motion m;
  std::copy_n(p,MotionBytes,m.bytes.begin());
  m.state=uint16_t(Get(p+StateOffset,2));
  return m;
}
struct Sample {
  Race race;
  uint32_t tick=0;
  uint8_t peer=0, mask=0;
  std::array<Motion,Slots> cars{};
};
inline Sample FromRow(Race race,const uint8_t* row,unsigned self,unsigned count,unsigned slots=Slots)
{
  Sample sample{race,Get(row+IndyMotion::TickOffset,4),uint8_t(self),0};
  if (!race || row[0]!=self || !RacingRow(row,count)) return sample;
  for (unsigned i=0;i<std::min(slots,Slots);++i)
  {
    const auto m=ReadMotion(Record(row,i));
    if (m) { sample.mask|=uint8_t(1u<<i); sample.cars[i]=m; }
  }
  return sample;
}
inline Wire Encode(const Sample& s)
{
  Wire wire{};
  std::memcpy(wire.data(),"IRM1",4);
  Put(wire.data()+4,s.race.epoch,4); Put(wire.data()+8,s.race.serial,4); Put(wire.data()+12,s.tick,4);
  wire[16]=s.race.roster; wire[17]=s.race.course; wire[18]=s.peer; wire[19]=s.mask;
  for (unsigned i=0;i<Slots;++i) if (s.mask&(1u<<i))
  {
    auto* p=wire.data()+20+i*(MotionBytes+2);
    std::copy(s.cars[i].bytes.begin(),s.cars[i].bytes.end(),p);
    Put(p+MotionBytes,s.cars[i].state,2);
  }
  return wire;
}
inline bool Decode(const uint8_t* p,size_t n,Race expected,unsigned peer,Sample& s)
{
  if (!expected || !peer || peer>Indy500Online::Cabinets || n!=Wire{}.size() || std::memcmp(p,"IRM1",4) ||
      Get(p+4,4)!=expected.epoch || Get(p+8,4)!=expected.serial || p[16]!=expected.roster ||
      p[17]!=expected.course || p[18]!=peer) return false; // mask zero is liveness only
  Sample v{expected,Get(p+12,4),p[18],p[19]};
  for (unsigned i=0;i<Slots;++i) if (v.mask&(1u<<i))
  {
    v.cars[i]=ReadMotion(p+20+i*(MotionBytes+2));
    if (!v.cars[i]) return false;
  }
  s=v;
  return true;
}

inline bool SameMotion(const Sample& a,const Sample& b)
{
  if (a.mask!=b.mask) return false;
  for (unsigned i=0;i<Slots;++i) if ((a.mask&(1u<<i)) &&
      (a.cars[i].state!=b.cars[i].state || a.cars[i].bytes!=b.cars[i].bytes)) return false;
  return true;
}

struct Known {
  Motion motion;
  uint32_t tick=0;
  uint64_t sampleUs=0; // 0 for a reliable sample (no inferred UDP transit age)
};
inline bool Matches(const Sample& sample,unsigned slot,uint16_t state,uint32_t nativeTick)
{
  const uint32_t gap=sample.tick-nativeTick;
  return (sample.mask&(1u<<slot)) && sample.cars[slot].state==state &&
         (gap<=MaxTickAhead || uint32_t(0-gap)<=MaxTickBehind);
}
inline bool Continuous(const Known& previous,const Sample& sample,unsigned slot)
{
  if (!previous.motion) return true;
  const uint32_t delta=sample.tick-previous.tick;
  return delta<=MaxTickAhead+MaxTickBehind &&
         IndyPoseUdp::Aligned(previous.motion.Pose(),sample.cars[slot].Pose(),delta) &&
         (delta || sample.cars[slot].bytes==previous.motion.bytes);
}

// Draw-only use of a recent race-UDP sample while reliable native motion is
// active. The caller independently established the native source mapping.
inline IndyPoseUdp::Selection PickDraw(const IndyMotion::Sample& native,uint16_t state,Race race,
                                      const Sample& remote,uint64_t sampleUs,uint64_t now)
{
  const uint32_t gap=remote.tick-native.tick;
  if (!race || remote.race.epoch!=race.epoch || remote.race.serial!=race.serial ||
      remote.race.roster!=race.roster || remote.race.course!=race.course ||
      !sampleUs || now<sampleUs || now-sampleUs>LeaveFreshUs ||
      !native.peer || native.peer!=remote.peer || native.slot>=Slots ||
      !(remote.mask&(1u<<native.slot)) || !gap || gap>IndyPoseUdp::MaxNewerTicks)
    return {};
  const auto& motion=remote.cars[native.slot];
  const auto pose=motion.Pose();
  if (!motion || motion.state!=state || !IndyPoseUdp::Aligned(pose,native.pose,gap)) return {};
  return {{pose,remote.tick,native.peer,native.slot},gap,now-sampleUs};
}

// The ordered TCP row still governs a car's state (and may contain a real
// reset/correction). A recent, continuous UDP position can lead that row;
// a periodic full row is not a reason to jump back to its older position.
inline Known VisibleMotion(const Known& ordered,const Known& previous,const Known& udp,
                           uint32_t controlTick,uint64_t now)
{
  if (!ordered.motion) return ordered;
  auto follows=[](const Known& earlier,const Known& later) {
    if (!earlier.motion || !later.motion || earlier.motion.state!=later.motion.state) return false;
    const uint32_t gap=later.tick-earlier.tick;
    return gap<=MaxTickAhead && (gap ? IndyPoseUdp::Aligned(earlier.motion.Pose(),later.motion.Pose(),gap) :
                                      earlier.motion.bytes==later.motion.bytes);
  };
  // A periodic full TCP row can be older than the continuous UDP position
  // already on screen. Never roll the car back to that stale anchor: when the
  // previous pose is a continuous, same-state lead, keep it as the base and
  // let the loop advance from there. A state change (reset/correction) still
  // takes the ordered row.
  Known visible=ordered;
  if (previous.motion && previous.tick>ordered.tick && previous.motion.state==ordered.motion.state &&
      follows(ordered,previous)) visible=previous;
  for (const Known* candidate:{&previous,&udp})
  {
    if (!candidate->sampleUs || now<candidate->sampleUs || now-candidate->sampleUs>LeaveFreshUs ||
        candidate->tick-controlTick>MaxTickAhead || candidate->tick==visible.tick ||
        !follows(ordered,*candidate) || !follows(visible,*candidate)) continue;
    // In the middle of a proper loss fallback, the last fresh pose can
    // cover the short gap while ordered TCP catches up. Never extrapolate.
    visible=*candidate;
  }
  return visible;
}

// IRP1 is the ordered TCP control pair. Only verified position/angle bytes
// are absent; all other native data survives exactly, including car state,
// flags, counters, course, rank and finish. FC is a heartbeat, not data:
// the padded tail is never copied into guest RAM by the comm board.
inline std::vector<uint8_t> Pack(const uint8_t* data,Holes holes,uint8_t fc,std::vector<uint8_t>* base=nullptr)
{
  std::array<uint8_t,Indy500Online::PayloadSize> control;
  std::copy_n(data,control.size(),control.begin());
  for (unsigned r=0;r<holes.size();++r)
    for (unsigned s=0;s<Slots;++s) if (holes[r]&(1u<<s))
      std::fill_n(Record(control.data()+1+r*IndyMotion::RowBytes,s),MotionBytes,0);
  // Once the preceding control row is known over TCP, send only the delta.
  // The UDP lane has independent, disposable samples and no TCP dependency.
  const bool delta=base && base->size()==control.size();
  auto changed=control;
  if (delta) for (unsigned i=0;i<changed.size();++i) changed[i]^=(*base)[i];
  const auto encoded=RelayUdp::Encode(changed.data(),changed.size());
  if (encoded.empty()) return {};
  std::vector<uint8_t> out(holes.begin(),holes.end());
  out.push_back(uint8_t(fc|(delta?0x80:0)));
  out.insert(out.end(),encoded.begin(),encoded.end());
  if (base) base->assign(control.begin(),control.end());
  return out;
}
inline bool Unpack(const std::vector<uint8_t>& wire,unsigned count,unsigned predecessor,
                   std::vector<uint8_t>& data,Holes& holes,uint8_t& fc,std::vector<uint8_t>* base=nullptr)
{
  using namespace Indy500Online;
  if (wire.size()<RacePairMinBytes || wire.size()>RacePairMaxBytes || count<2 || count>Cabinets ||
      !predecessor || predecessor>count || (wire[sizeof(Holes)]&0x7f)!=1) return false;
  const bool delta=(wire[sizeof(Holes)]&0x80)!=0;
  if (delta && (!base || base->size()!=PayloadSize)) return false;
  const std::vector<uint8_t> encoded(wire.begin()+RacePairHeaderBytes,wire.end());
  std::vector<uint8_t> raw;
  if (!RelayUdp::Decode(encoded,PayloadSize,raw)) return false;
  if (delta) for (unsigned i=0;i<raw.size();++i) raw[i]^=(*base)[i];
  if (raw[0]!=predecessor) return false;
  Holes masks{};
  std::copy_n(wire.begin(),masks.size(),masks.begin());
  bool any=false;
  for (unsigned r=0;r<masks.size();++r) if (masks[r])
  {
    const auto* row=raw.data()+1+r*IndyMotion::RowBytes;
    if (!RacingRow(row,count) || row[0]!=1+(predecessor-1+r)%count) return false;
    for (unsigned s=0;s<Slots;++s) if (masks[r]&(1u<<s))
    {
      const auto* p=Record(row,s);
      if ((Get(p+StateOffset,2)&15)!=1 || std::any_of(p,p+MotionBytes,[](uint8_t v){return v!=0;})) return false;
      any=true;
    }
  }
  if (!any) return false;
  holes=masks;
  fc=wire[sizeof(Holes)]&0x7f;
  if (base) *base=raw;
  data.swap(raw);
  return true;
}
}
#endif
