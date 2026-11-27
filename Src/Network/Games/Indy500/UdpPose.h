// SPDX-License-Identifier: GPL-3.0-or-later
// INDY 500 pose sidecar. Explicit little-endian fields; no native game packet
// is sent or reconstructed from UDP. This is presentation input only.
#ifndef INCLUDED_INDY500_UDP_POSE_H
#define INCLUDED_INDY500_UDP_POSE_H

#include "Motion.h"
#include "Network/Online/Indy500.h"
#include <array>
#include <cstring>

namespace IndyPoseUdp
{
using IndyMotion::Pose;
using IndyMotion::Sample;
using IndyMotion::Vec;
using Bytes = std::array<uint8_t, Indy500Online::PoseWireBytes>;
constexpr unsigned Slots = Indy500Online::PoseSlots;
constexpr uint64_t MaxAgeUs = 120000;
constexpr unsigned MaxNewerTicks = 16;
static_assert(Slots * 2 == IndyMotion::MaxActors && Bytes{}.size() == 20 + Slots * 12);

struct Race
{
  uint32_t epoch = 0, serial = 0;
  uint8_t roster = 0, course = 0;
  explicit operator bool() const { return epoch && serial && (roster & (roster-1)) && course <= 2; }
};
struct Packet
{
  Race race;
  uint32_t tick = 0;
  uint8_t seat = 0, mask = 0;
  std::array<Pose,Slots> poses{};
};
struct Received
{
  Packet packet;
  uint64_t stamp = 0, receivedUs = 0, ageUs = 0;
  bool timed = false;
};

inline void Put(uint8_t* buf,uint32_t value,unsigned size)
{
  for (unsigned i=0;i<size;++i) buf[i]=uint8_t(value>>(8*i));
}
inline uint32_t Get(const uint8_t* buf,unsigned size)
{
  uint32_t word=0;
  for (unsigned i=0;i<size;++i) word|=uint32_t(buf[i])<<(8*i);
  return word;
}
inline void PutPosition(uint8_t* buf,double v)
{
  const float f=float(v); uint32_t word;
  std::memcpy(&word,&f,sizeof(word)); Put(buf,word,4);
}
inline double GetPosition(const uint8_t* buf)
{
  const uint32_t word=Get(buf,4); float f;
  std::memcpy(&f,&word,sizeof(f)); return f;
}
inline Bytes Encode(const Packet& packet)
{
  Bytes b{};
  std::memcpy(b.data(),"IPU1",4);
  Put(b.data()+4,packet.race.epoch,4); Put(b.data()+8,packet.race.serial,4); Put(b.data()+12,packet.tick,4);
  b[16]=packet.race.roster; b[17]=packet.race.course; b[18]=packet.seat; b[19]=packet.mask;
  for (unsigned slot=0;slot<Slots;++slot) if (packet.mask & (1u<<slot))
  {
    auto* p=b.data()+20+12*slot;
    PutPosition(p,packet.poses[slot].p.x); PutPosition(p+4,packet.poses[slot].p.z);
    Put(p+8,packet.poses[slot].yaw,2);
  }
  return b;
}
inline bool Decode(const uint8_t* b,size_t size,Race current,unsigned peer,Packet& out)
{
  if (!current || !peer || peer>IndyMotion::MaxSeats || size!=Bytes{}.size() || std::memcmp(b,"IPU1",4) ||
      Get(b+4,4)!=current.epoch || Get(b+8,4)!=current.serial || b[16]!=current.roster || b[17]!=current.course ||
      b[18]!=peer || !b[19]) return false;
  Packet packet{current,Get(b+12,4),b[18],b[19]};
  for (unsigned slot=0;slot<Slots;++slot) if (packet.mask & (1u<<slot))
  {
    const auto* p=b+20+12*slot;
    const Pose pose{{GetPosition(p),GetPosition(p+4)},uint16_t(Get(p+8,2))};
    if (Get(p+10,2) || !IndyMotion::ValidPosition(pose.p)) return false;
    packet.poses[slot]=pose;
  }
  out=packet; return true;
}

// Never substitute a pose from ahead of a native discontinuity, nor an old
// datagram that happened to arrive last. The peer and slot were independently
// established by the unchanged ROM/TCP source mapping at the call site.
inline bool Aligned(Pose a,Pose b,uint32_t ticks)
{
  return (a.p-b.p).Length() <= 3.5*ticks && std::abs(IndyMotion::YawDelta(a.yaw,b.yaw)) <= 700*int(ticks);
}
struct Selection
{
  Sample pose;
  unsigned gap = 0;
  uint64_t age = 0;
  explicit operator bool() const { return gap != 0; }
};
inline Selection Pick(const Sample& native,const Received& remote,uint64_t now)
{
  const auto& packet=remote.packet;
  const uint32_t gap=packet.tick-native.tick;
  if (!remote.timed || !remote.receivedUs || now<remote.receivedUs || now-remote.receivedUs>MaxAgeUs ||
      remote.ageUs>MaxAgeUs-(now-remote.receivedUs) || !gap || gap>MaxNewerTicks || packet.seat!=native.peer ||
      native.slot>=Slots || !(packet.mask & (1u<<native.slot)) ||
      !Aligned(packet.poses[native.slot],native.pose,gap)) return {};
  return {{packet.poses[native.slot],packet.tick,native.peer,native.slot},gap,remote.ageUs+now-remote.receivedUs};
}
inline Pose Bound(Pose native,Pose shown)
{
  const Vec d=shown.p-native.p;
  const double len=d.Length();
  if (len>IndyMotion::MaxShift) shown.p=native.p+d*(IndyMotion::MaxShift/len);
  shown.yaw=uint16_t(int(native.yaw)+std::clamp(IndyMotion::YawDelta(shown.yaw,native.yaw),
                       -IndyMotion::MaxYawShift,IndyMotion::MaxYawShift));
  return shown;
}

// TCP prediction remains warm as a fallback. Fade only the extra UDP
// correction, then enforce the original caps relative to the native pose.
class Blend
{
public:
  void Clear() { *this={}; }
  unsigned Weight() const { return m_weight; }
  Pose Step(Sample native,Pose tcp,const Pose* udp,uint64_t frame,bool ease=false)
  {
    if (!m_valid || frame!=m_frame)
    {
      const uint32_t ticks=native.tick-m_native.tick;
      if (!m_valid || frame<m_frame || frame-m_frame>2 || native.peer!=m_native.peer || native.slot!=m_native.slot ||
          ticks>8 || !Aligned(native.pose,m_native.pose,ticks)) Clear();
      m_native=native; m_frame=frame; m_valid=true;
      if (udp)
      {
        const auto bounded=Bound(native.pose,*udp);
        const Vec target=bounded.p-tcp.p;
        const int yaw=IndyMotion::YawDelta(bounded.yaw,tcp.yaw);
        if (ease)
        {
          if (!m_weight) { m_offset={}; m_yaw=0; }
          m_offset=m_offset+(target-m_offset)*0.25;
          m_yaw+=int(std::lround((yaw-m_yaw)*0.25));
        }
        else { m_offset=target; m_yaw=yaw; }
        if (m_weight<4) ++m_weight;
      }
      else if (m_weight) --m_weight;
    }
    if (!m_weight) return tcp;
    const double w=m_weight*0.25;
    return Bound(native.pose,{tcp.p+m_offset*w,
      uint16_t(int(tcp.yaw)+int(std::lround(w*m_yaw)))});
  }
private:
  Sample m_native{};
  Vec m_offset{};
  uint64_t m_frame=0;
  int m_yaw=0;
  unsigned m_weight=0;
  bool m_valid=false;
};
}
#endif
