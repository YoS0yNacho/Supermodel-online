// SPDX-License-Identifier: GPL-3.0-or-later
// Sega Rally Championship Twin/DX Rev C: render-only remote motion.
#ifndef INCLUDED_SRALLYC_MOTION_H
#define INCLUDED_SRALLYC_MOTION_H

#include "Network/Online/Srallyc.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace SrallycMotion
{
inline constexpr unsigned Tx=0x2000, Rx=0x21c0, RowBytes=0x1c0, HeaderBytes=0x54;
inline constexpr double FrameUs=1000000.0/SrallycOnline::RefreshRate;
inline constexpr double MaxShift=6.0, MaxYawShift=0.16, TwoPi=6.283185307179586477;
inline constexpr unsigned HistorySize=24;
inline constexpr uint64_t MaxUdpAgeUs=300000, MaxUncertaintyUs=80000;

struct Vec
{
  double x=0,z=0;
  Vec operator+(Vec b) const { return {x+b.x,z+b.z}; }
  Vec operator-(Vec b) const { return {x-b.x,z-b.z}; }
  Vec operator*(double f) const { return {x*f,z*f}; }
  double Dot(Vec b) const { return x*b.x+z*b.z; }
  double Length() const { return std::hypot(x,z); }
};
struct Pose { Vec p{}; double yaw=0; };
struct Scene
{
  uint32_t race=0,course=0,part=0;
  explicit operator bool() const { return race && course<32 && part<32; }
  bool operator==(Scene b) const { return race==b.race && course==b.course && part==b.part; }
  bool operator!=(Scene b) const { return !(*this==b); }
};
struct Row
{
  Scene scene{};
  Pose pose{};
  Vec velocity{}; // live row +0x34/+0x3c, in position units per native frame
  uint64_t key=0;
  unsigned seat=0;
  explicit operator bool() const { return key && seat>=1 && seat<=SrallycOnline::Cabinets && bool(scene); }
};
inline uint64_t Get(const uint8_t* p,unsigned n)
{
  uint64_t x=0;for (unsigned i=0;i<n;++i) x|=uint64_t(p[i])<<(8*i);return x;
}
inline void Put(uint8_t* p,uint64_t x,unsigned n)
{
  for (unsigned i=0;i<n;++i) p[i]=uint8_t(x>>(8*i));
}
inline float AsFloat(uint32_t word)
{
  float f; std::memcpy(&f,&word,4); return f;
}
inline uint32_t FloatBits(float f)
{
  uint32_t word; std::memcpy(&word,&f,4); return word;
}
inline double Float(const uint8_t* p) { return AsFloat(uint32_t(Get(p,4))); }
inline bool Valid(Pose p,Vec v)
{
  // Use ordered comparisons so NaN/Inf cannot enter the display path.
  return std::abs(p.p.x)<8192 && std::abs(p.p.z)<8192 && std::abs(p.yaw)<64 && v.Length()<2.5;
}
inline Row DecodeRow(const uint8_t* row,size_t n,unsigned seat)
{
  if (n<HeaderBytes || !seat || seat>SrallycOnline::Cabinets || Get(row,2)!=seat ||
      Get(row+2,2)!=seat || Get(row+0x18,4)!=6 || !(std::abs(Float(row+0x2c))<1024)) return {};
  Row a;
  a.scene={uint32_t(Get(row+0x1c,4)),uint32_t(Get(row+0x20,4)),uint32_t(Get(row+0x24,4))};
  a.pose={{Float(row+0x28),Float(row+0x30)},Float(row+0x44)};
  a.velocity={Float(row+0x34),Float(row+0x3c)};
  if (!a.scene || !Valid(a.pose,a.velocity)) return {};
  // The unchanged ring may hold several snapshots for the same seat. Match
  // the exact in-race native row, not arrival order or an assumed common tick.
  // Authentication is the relay session token; this is just a row fingerprint.
  uint64_t key=14695981039346656037ull;
  for (unsigned i=0;i<HeaderBytes;++i) key=(key^row[i])*1099511628211ull;
  a.key=key ? key : 1;
  a.seat=seat;
  return a;
}
inline double YawDiff(double a,double b) { return std::remainder(a-b,TwoPi); }
inline Pose Bound(Pose native,Pose p)
{
  const Vec d=p.p-native.p;
  const double n=d.Length();
  if (n>MaxShift) p.p=native.p+d*(MaxShift/n);
  p.yaw=native.yaw+std::clamp(YawDiff(p.yaw,native.yaw),-MaxYawShift,MaxYawShift);
  return p;
}
// A cap measured from the native pose is stable when a newer donor
// projects closer to the local car. All points on the allowed shift stay
// outside the contact radius by the triangle inequality. Do not shrink the
// warm TCP pose based on (or reset it because of) a transient UDP target.
inline constexpr double ContactRadius=5.0;
inline Pose LimitContact(Pose native,Pose target,double nativeDistance)
{
  const double limit=std::clamp(0.4*(nativeDistance-ContactRadius),0.,MaxShift);
  const Vec shift=target.p-native.p;
  const double length=shift.Length();
  const double yawLimit=MaxYawShift*limit/MaxShift;
  return {length>limit ? native.p+shift*(limit/length) : target.p,
          native.yaw+std::clamp(YawDiff(target.yaw,native.yaw),-yawLimit,yawLimit)};
}
inline bool Continuous(const Row& a,const Row& b,double ticks)
{
  if (!a || !b || a.seat!=b.seat || a.scene!=b.scene || ticks<1 || ticks>HistorySize) return false;
  const auto d=b.pose.p-a.pose.p;
  const auto v=(a.velocity+b.velocity)*0.5;
  const double speed=v.Length();
  return d.Length()<=0.7+std::min(2.5,speed+0.65)*ticks &&
         std::abs(YawDiff(b.pose.yaw,a.pose.yaw))<=0.14*ticks+0.06 &&
         (b.velocity-a.velocity).Length()<1.15 &&
         d.Dot(v)>=-0.16*speed; // do not coast through a backward update
}

using Wire=std::array<uint8_t,SrallycOnline::PoseWireBytes>;
static_assert(Wire{}.size()==48+8*HistorySize);
struct Packet
{
  Row row{};
  uint32_t seq=0;
  unsigned n=0;
  std::array<uint64_t,HistorySize> anchors{}; // [0] is the accompanying pose, older entries follow
};
inline Wire Encode(const Packet& p)
{
  Wire b{};
  std::memcpy(b.data(),"SRP1",4);
  Put(b.data()+4,p.row.scene.race,4); Put(b.data()+8,p.row.scene.course,4); Put(b.data()+12,p.row.scene.part,4);
  Put(b.data()+16,p.seq,4); b[20]=uint8_t(p.row.seat); b[21]=uint8_t(p.n);
  const double values[]={p.row.pose.p.x,p.row.pose.p.z,p.row.pose.yaw,p.row.velocity.x,p.row.velocity.z};
  for (unsigned i=0;i<5;++i) Put(b.data()+24+i*4,FloatBits(float(values[i])),4);
  for (unsigned i=0;i<std::min(p.n,HistorySize);++i) Put(b.data()+48+i*8,p.anchors[i],8);
  return b;
}
inline bool Decode(const uint8_t* b,size_t bytes,unsigned origin,Packet& out)
{
  if (bytes!=Wire{}.size() || std::memcmp(b,"SRP1",4) || !origin || origin>SrallycOnline::Cabinets ||
      b[20]!=origin || !b[21] || b[21]>HistorySize || Get(b+22,2) || Get(b+44,4)) return false;
  Packet p;
  p.row.scene={uint32_t(Get(b+4,4)),uint32_t(Get(b+8,4)),uint32_t(Get(b+12,4))};
  p.seq=uint32_t(Get(b+16,4)); p.n=b[21];p.row.seat=origin;
  p.row.pose={{Float(b+24),Float(b+28)},Float(b+32)};
  p.row.velocity={Float(b+36),Float(b+40)};
  if (!p.row.scene || p.seq<p.n || !Valid(p.row.pose,p.row.velocity)) return false;
  for (unsigned i=0;i<HistorySize;++i)
  {
    const uint64_t key=Get(b+48+i*8,8);
    if (bool(key)!=(i<p.n)) return false;
    p.anchors[i]=key;
  }
  p.row.key=p.anchors[0];
  out=p; return true;
}

class Publisher
{
public:
  void Clear() { m_p={}; }
  const Packet* Push(const Row& p)
  {
    if (!p) { Clear(); return nullptr; }
    if (!m_p.row || !Continuous(m_p.row,p,6) || m_p.seq==UINT32_MAX) Clear();
    if (m_p.row && m_p.row.key==p.key) return nullptr;
    m_p.row=p; ++m_p.seq;
    std::move_backward(m_p.anchors.begin(),m_p.anchors.end()-1,m_p.anchors.end());
    m_p.anchors[0]=p.key;
    m_p.n=std::min(m_p.n+1,HistorySize);
    return &m_p;
  }
private:
  Packet m_p{};
};
struct Received
{
  Packet p{};
  uint64_t stamp=0,received=0,age=0,uncertainty=0;
  double periodUs=0;
};
struct Pick
{
  Row row{};
  unsigned gap=0;
  uint32_t seq=0;
  uint64_t age=0;
  explicit operator bool() const { return bool(row); }
};
inline Pick Latest(const Row& native,const Received& r,uint64_t now)
{
  if (!native || !r.stamp || !r.received || now<r.received || r.age>MaxUdpAgeUs ||
      now-r.received>MaxUdpAgeUs-r.age || !r.p.row || native.scene!=r.p.row.scene || native.seat!=r.p.row.seat) return {};
  // Do not substitute an older datagram merely because it arrived after TCP.
  // A newest matching anchor also handles repeated exact native rows safely.
  for (unsigned i=0;i<r.p.n && i<HistorySize;++i)
  {
    if (r.p.anchors[i]!=native.key) continue;
    if (i && Continuous(native,r.p.row,double(i)+1)) return {r.p.row,i,r.p.seq,r.age+now-r.received};
    return {};
  }
  return {};
}

struct Draw { Pose shown{},tcp{}; Pick udp{}; unsigned weight=0; };
class Predictor
{
public:
  void Clear() { *this={}; }
  Draw Step(const Row& a,const Row& local,const Received& latest,uint64_t frame,uint64_t now,double frameUs=FrameUs)
  {
    if (!a || !local || local.seat==a.seat || local.scene!=a.scene) { Clear(); return {a.pose,a.pose}; }
    if (!m_last || frame<m_frame || frame-m_frame>3 || a.seat!=m_last.seat || a.scene!=m_last.scene)
      return Seed(a,frame);
    if (frame==m_frame) return a.key==m_last.key ? m_draw : Draw{a.pose,a.pose};
    const double distance=(local.pose.p-a.pose.p).Length();
    if (distance<=ContactRadius) return Seed(a,frame);
    const auto step=double(frame-m_frame);
    const auto gap=frame-m_seen;
    if (a.key!=m_last.key)
    {
      if (gap>9 || !Continuous(m_last,a,double(gap)+3)) return Seed(a,frame);
      const auto dv=(m_last.velocity+a.velocity)*0.5;
      const auto difference=a.pose.p-m_last.pose.p;
      const auto v2=dv.Dot(dv);
      const double dt=v2>0.003 ? std::clamp(difference.Dot(dv)/v2,1.,12.) : double(std::max<uint64_t>(1,gap));
      const double instant=std::clamp(YawDiff(a.pose.yaw,m_last.pose.yaw)/dt,-0.08,0.08);
      m_turn=m_updates ? (0.65*m_turn+0.35*instant) : instant;
      m_seen=frame; m_last=a; m_updates=std::min(m_updates+1,10u);
    }
    const double stale=double(frame-m_seen);
    const double speed=a.velocity.Length();
    if (!m_updates || stale>=9 || speed<0.035) return Seed(a,frame);
    const double fresh=std::clamp((9.0-stale)/4.0,0.,1.);
    const double ahead=std::min(5.0,0.9+stale)*fresh;
    const auto target=a.pose.p+a.velocity*ahead;
    const auto carry=m_draw.tcp.p+a.velocity*(fresh*step);
    const double correction=std::max(0.22,std::min(1.0,(target-carry).Length()/2.8));
    Pose tcp=Bound(a.pose,{carry+(target-carry)*correction,a.pose.yaw+m_turn*ahead});
    Pick udp=Latest(a,latest,now);
    if (udp)
    {
      // The source can be running slower than nominal on real cabinets.
      // Do not turn 45 ms on a slow host into three native frames of lead.
      // Consume the newer measured pose first; project at most one step,
      // once a sender cadence is known and only beyond clock uncertainty.
      const double margin=std::max(double(latest.uncertainty),0.35*latest.periodUs);
      const double coast=latest.periodUs>=frameUs ? std::clamp((double(udp.age)-margin)/latest.periodUs,0.,1.) : 0.0;
      const double turn=std::clamp(YawDiff(udp.row.pose.yaw,a.pose.yaw)/double(udp.gap),-0.08,0.08);
      const Pose targetUdp=Bound(a.pose,{udp.row.pose.p+udp.row.velocity*coast,udp.row.pose.yaw+turn*coast});
      m_offset=targetUdp.p-tcp.p; m_yawOffset=YawDiff(targetUdp.yaw,tcp.yaw);
      m_weight=std::min(m_weight+1,4u);
    }
    else if (m_weight) --m_weight;
    const double mix=m_weight*0.25;
    Pose shown=Bound(a.pose,{tcp.p+m_offset*mix,tcp.yaw+m_yawOffset*mix});
    m_frame=frame;
    return m_draw={LimitContact(a.pose,shown,distance),LimitContact(a.pose,tcp,distance),udp,m_weight};
  }
private:
  Draw Seed(const Row& a,uint64_t frame)
  {
    Clear(); m_frame=m_seen=frame;m_last=a;
    return m_draw={a.pose,a.pose};
  }
  uint64_t m_frame=0,m_seen=0;
  unsigned m_updates=0,m_weight=0;
  double m_turn=0,m_yawOffset=0;
  Vec m_offset{};
  Row m_last{};
  Draw m_draw{};
};
}
#endif
