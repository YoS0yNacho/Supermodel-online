// Original Daytona USA Rev A: presentation only; no writes to the guest.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_DAYTONA_MOTION_H
#define INCLUDED_DAYTONA_MOTION_H
#include "Network/Online/Daytona.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace DaytonaMotion {
inline constexpr unsigned Tx=0x2000, Rx=0x21c0, RowBytes=0x1c0, PoseBytes=0xa0;
inline constexpr uint32_t RaceState=0x00400000, ActorBase=0x505180, ActorStride=0x300;
inline constexpr double FrameUs=1000000.0/DaytonaOnline::RefreshRate;
inline constexpr double Tau=6.2831853071795864769, AngleUnit=Tau/65536.0;
inline constexpr double ContactRadius=5.5, MaxShift=2.75, MaxYaw=0.085, MaxSpeed=2.65;
inline constexpr uint64_t FreshUs=180000, MaxUncertaintyUs=80000;
inline constexpr unsigned History=24;

struct Vec {
  double x=0,z=0;
  Vec operator+(Vec b) const {return {x+b.x,z+b.z};}
  Vec operator-(Vec b) const {return {x-b.x,z-b.z};}
  Vec operator*(double w) const {return {x*w,z*w};}
  double Length() const {return std::hypot(x,z);}
};
struct Pose {Vec p{}; double yaw=0;};
struct Scene {
  uint32_t epoch=0;
  unsigned course=0;
  explicit operator bool() const {return epoch && course<3;}
  bool operator==(Scene b) const {return epoch==b.epoch && course==b.course;}
  bool operator!=(Scene b) const {return !(*this==b);}
};
struct Row {
  Pose pose{};
  Scene scene{};
  uint64_t key=0;
  unsigned seat=0;
  uint8_t tick=0;
  explicit operator bool() const {return key && seat && seat<=DaytonaOnline::Cabinets && bool(scene);}
};
inline uint64_t Get(const uint8_t* b,unsigned n) {
  uint64_t v=0;for (unsigned i=0;i<n;++i) v|=uint64_t(b[i])<<(i*8);return v;
}
inline void Put(uint8_t* b,uint64_t v,unsigned n) {
  for (unsigned i=0;i<n;++i) b[i]=uint8_t(v>>(i*8));
}
inline float AsFloat(uint32_t v) {float f;std::memcpy(&f,&v,4);return f;}
inline uint32_t AsWord(float f) {uint32_t v;std::memcpy(&v,&f,4);return v;}
inline double Angle(uint16_t bits) {return std::remainder(bits*AngleUnit,Tau);}
inline double Turn(double a,double b) {return std::remainder(a-b,Tau);}
inline bool Valid(Pose p) {return std::abs(p.p.x)<4096 && std::abs(p.p.z)<4096 && std::abs(p.yaw)<=Tau/2.;}
inline Row DecodeRow(const uint8_t* b,size_t n,unsigned seat,Scene scene) {
  // Both the local participant phase and the room phase must be racing.
  // +0x16 follows the master only; a racing slave still advertises 0x12 there.
  if (!b || n<PoseBytes || !scene || !seat || seat>DaytonaOnline::Cabinets || b[0x1b]!=0x16 || b[0x1d]!=0x16) return {};
  uint32_t sum=0;
  for (unsigned i=0;i<0x3c;i+=4) sum+=uint32_t(Get(b+i,4));
  if (sum!=Get(b+0x3c,4) || !(std::abs(AsFloat(uint32_t(Get(b+0x60,4))))<512)) return {};
  const Pose p={{AsFloat(uint32_t(Get(b+0x5c,4))),AsFloat(uint32_t(Get(b+0x64,4)))},Angle(uint16_t(Get(b+0x8e,2)))};
  if (!Valid(p)) return {};
  uint64_t h=14695981039346656037ull;
  for (unsigned i=0;i<PoseBytes;++i) h=(h^b[i])*1099511628211ull;
  return {p,scene,h?h:1,seat,b[0x0a]};
}
inline unsigned Ticks(const Row& from,const Row& to) {return uint8_t(to.tick-from.tick);}
inline bool Continuous(const Row& from,const Row& to) {
  if (!from || !to || from.seat!=to.seat || from.scene!=to.scene) return false;
  const unsigned ticks=Ticks(from,to);
  return ticks && ticks<=History && (to.pose.p-from.pose.p).Length()<=0.45+MaxSpeed*ticks &&
    std::abs(Turn(to.pose.yaw,from.pose.yaw))<=0.20*ticks+0.03;
}
inline Pose Bound(Pose native,Pose p,double shift=MaxShift) {
  const Vec d=p.p-native.p;
  const double length=d.Length(), ang=MaxYaw*shift/MaxShift;
  return {length>shift ? native.p+d*(shift/length) : p.p,
          native.yaw+std::clamp(Turn(p.yaw,native.yaw),-ang,ang)};
}

using Wire=std::array<uint8_t,DaytonaOnline::PoseWireBytes>;
static_assert(Wire{}.size()==32+History*8);
struct Packet {Row row{};uint32_t seq=0;unsigned n=0;std::array<uint64_t,History> keys{};};
inline Wire Encode(const Packet& p) {
  Wire w{};std::memcpy(w.data(),"DYP1",4);
  Put(w.data()+4,p.row.scene.epoch,4);Put(w.data()+8,p.seq,4);
  w[12]=uint8_t(p.row.seat);w[13]=uint8_t(p.row.scene.course);w[14]=p.row.tick;w[15]=uint8_t(p.n);
  Put(w.data()+16,AsWord(float(p.row.pose.p.x)),4);Put(w.data()+20,AsWord(float(p.row.pose.p.z)),4);
  // Carry the exact 16-bit native rotation, avoiding a rounded +/-pi on decode.
  Put(w.data()+24,uint16_t(std::lround(p.row.pose.yaw/AngleUnit)),2);
  for (unsigned i=0;i<std::min(p.n,History);++i) Put(w.data()+32+8*i,p.keys[i],8);
  return w;
}
inline bool Decode(const uint8_t* b,size_t size,unsigned origin,Packet& out) {
  if (!b || size!=Wire{}.size() || std::memcmp(b,"DYP1",4) || !origin || origin>DaytonaOnline::Cabinets ||
      b[12]!=origin || !b[15] || b[15]>History || Get(b+26,6)) return false;
  Packet p;
  p.row.scene={uint32_t(Get(b+4,4)),b[13]};p.row.seat=b[12];p.row.tick=b[14];p.n=b[15];p.seq=uint32_t(Get(b+8,4));
  p.row.pose={{AsFloat(uint32_t(Get(b+16,4))),AsFloat(uint32_t(Get(b+20,4)))},Angle(uint16_t(Get(b+24,2)))};
  if (!p.row.scene || !Valid(p.row.pose) || p.seq<p.n) return false;
  for (unsigned i=0;i<History;++i) {
    p.keys[i]=Get(b+32+8*i,8);
    if (bool(p.keys[i])!=(i<p.n)) return false;
  }
  p.row.key=p.keys[0];out=p;return true;
}
class Publisher {
public:
  void Clear() {m={};}
  const Packet* Push(Row r) {
    if (!r) {Clear();return nullptr;}
    if (m.row && m.row.scene==r.scene && m.row.key==r.key) return nullptr;
    if (m.seq==UINT32_MAX || !Continuous(m.row,r)) Clear();
    m.row=r; ++m.seq;
    std::move_backward(m.keys.begin(),m.keys.end()-1,m.keys.end());
    m.keys[0]=r.key;m.n=std::min(History,m.n+1);return &m;
  }
private:
  Packet m{};
};
struct Received {Packet p{};uint64_t received=0,age=0,uncertainty=0,stamp=0;};
struct Pick {Row row{};unsigned gap=0;uint32_t seq=0;uint64_t age=0;explicit operator bool()const{return bool(row);}};
inline Pick Latest(const Row& native,const Received& r,uint64_t now) {
  if (!r.received || r.uncertainty>MaxUncertaintyUs || r.age>FreshUs || now<r.received ||
      now-r.received>FreshUs-r.age || !Continuous(native,r.p.row)) return {};
  // Require the exact currently consumed native row, at an older position
  // than the UDP pose. The session token, not this fingerprint, authenticates.
  if (r.p.keys[0]==native.key) return {};
  for (unsigned i=1;i<r.p.n && i<History;++i)
    if (r.p.keys[i]==native.key) return {r.p.row,Ticks(native,r.p.row),r.p.seq,r.age+now-r.received};
  return {};
}

struct Draw {Pose tcp{},shown{};Pick udp{};unsigned weight=0;};
class Predictor {
public:
  void Clear() {*this={};}
  Draw Step(Row r,Pose native,double nearest,const Received& latest,uint64_t frame,uint64_t now,double frameUs=FrameUs) {
    if (!r || !Valid(native) || !(nearest>ContactRadius) || !m_row || r.scene!=m_row.scene ||
        r.seat!=m_row.seat || frame<=m_frame || frame-m_frame>3 || (native.p-r.pose.p).Length()>4.5)
      return Seed(r,native,frame);
    if (r.key!=m_row.key) {
      if (frame-m_seen>9 || !Continuous(m_row,r)) return Seed(r,native,frame);
      const double inv=1.0/Ticks(m_row,r);
      const Vec v=(r.pose.p-m_row.pose.p)*inv;
      if (v.Length()>MaxSpeed) return Seed(r,native,frame);
      const double turn=std::clamp(Turn(r.pose.yaw,m_row.pose.yaw)*inv,-0.08,0.08);
      m_velocity=m_warm ? m_velocity*.53+v*.47 : v;
      m_turn=m_warm ? m_turn*.61+turn*.39 : turn;
      m_warm=true;m_seen=frame;m_row=r;
    }
    const auto stale=frame-m_seen;
    if (!m_warm || stale>=9 || m_velocity.Length()<.025) return Seed(r,native,frame);
    const double elapsed=double(frame-m_frame);
    const double boost=std::clamp((9.-double(stale))/4.,0.,1.);
    // Daytona already advances the remote actor between imports. Do not
    // replace that pose with a second prediction of the older RX anchor.
    // Carry display velocity for continuity, then converge on the live actor.
    const Vec coast=m_tcp.p+m_velocity*(elapsed*boost);
    const double yawCoast=m_tcp.yaw+m_turn*elapsed*boost;
    m_tcp=Bound(native,{native.p+(coast-native.p)*.40,
                         native.yaw+Turn(yawCoast,native.yaw)*.30},MaxShift*.70);
    const auto udp=Latest(r,latest,now);
    if (udp) {
      // At most one additional measured native step. Aim at the newer owner
      // pose rather than projecting several frames based on nominal refresh.
      const Vec vel=(udp.row.pose.p-r.pose.p)*(1.0/udp.gap);
      const double coast=std::clamp((double(udp.age)-double(latest.uncertainty))/frameUs,0.,1.);
      const Pose newer=Bound(m_tcp,{udp.row.pose.p+vel*coast,udp.row.pose.yaw+m_turn*coast},1.7);
      const Vec correction=newer.p-m_tcp.p-m_extra;
      const double len=correction.Length(), rate=.32*elapsed;
      m_extra=m_extra+(len>rate ? correction*(rate/len) : correction);
      m_extraYaw+=std::clamp(Turn(newer.yaw,m_tcp.yaw)-m_extraYaw,-.012*elapsed,.012*elapsed);
      m_weight=std::min(m_weight+1,4u);
    } else if (m_weight) --m_weight;
    if (!m_weight) { m_extra={};m_extraYaw=0; }
    const double limit=std::clamp(.32*(nearest-ContactRadius),0.,MaxShift), w=m_weight*.25;
    m_frame=frame;
    return {Bound(native,m_tcp,limit),Bound(native,{m_tcp.p+m_extra*w,m_tcp.yaw+m_extraYaw*w},limit),udp,m_weight};
  }
private:
  Draw Seed(Row r,Pose native,uint64_t frame) {
    Clear();m_row=r;m_frame=m_seen=frame;m_tcp=native;
    return {native,native};
  }
  Row m_row{};Pose m_tcp{};Vec m_velocity{},m_extra{};
  uint64_t m_frame=0,m_seen=0;
  double m_turn=0,m_extraYaw=0;
  unsigned m_weight=0;
  bool m_warm=false;
};
}
#endif
