// SPDX-License-Identifier: GPL-3.0-or-later
// Receiver-side presentation for INDY 500 Twin Rev A (Newer). No wire format.
#ifndef INCLUDED_INDY500_MOTION_H
#define INCLUDED_INDY500_MOTION_H

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace IndyMotion
{
constexpr unsigned MaxSeats = 8, MaxActors = 16;
constexpr unsigned RowBytes = 0x1c0, RecordBytes = 0x18;
constexpr unsigned Rx = 0x21c0, PayloadOffset = 0x24, TickOffset = 0x14;
constexpr uint32_t GuestRx = 0x1a10000 + Rx + PayloadOffset;
constexpr double FrameUs = 17384.0;  // Model 2: 424 lines × 41 us
constexpr double MaxLead = 6.0, MaxCoast = 8.0, MaxShift = 12.0;
constexpr int MaxYawShift = 1536;    // 8.44 degrees

struct Vec
{
  double x = 0, z = 0;
  Vec operator+(Vec b) const { return {x+b.x,z+b.z}; }
  Vec operator-(Vec b) const { return {x-b.x,z-b.z}; }
  Vec operator*(double a) const { return {x*a,z*a}; }
  double Dot(Vec b) const { return x*b.x+z*b.z; }
  double Length() const { return std::sqrt(Dot(*this)); }
};
inline bool ValidPosition(Vec p)
{
  // Reject non-finite data and the native -10000 empty-slot sentinel.
  return std::abs(p.x)<8192.0 && std::abs(p.z)<8192.0;
}
inline int SignedYaw(uint16_t a) { return a<32768 ? int(a) : int(a)-65536; }
inline int YawDelta(uint16_t a, uint16_t b) { return SignedYaw(uint16_t(a-b)); }

struct Record
{
  unsigned row = 0, slot = 0;
  explicit operator bool() const { return slot<MaxActors; }
};
inline Record FindRecord(unsigned actor, unsigned owner, unsigned players, unsigned local,
                         unsigned mappedRow, unsigned count, uint32_t source)
{
  const Record invalid{0,MaxActors};
  if (count<2 || count>MaxSeats || players<2 || players>count || actor>=MaxActors ||
      local>=players || owner>=players || owner==local || actor%players!=owner ||
      mappedRow>=count || source<GuestRx) return invalid;
  const unsigned slot=actor/players;
  const uint32_t expected=GuestRx + RowBytes*mappedRow + RecordBytes*slot;
  return source==expected ? Record{mappedRow,slot} : invalid;
}

struct Pose { Vec p; uint16_t yaw=0; };
// The display may move on its own by at most MaxRate + 1.5*speed per frame.
constexpr double MaxRate = 1.0;
inline Vec LimitMove(Vec from, Vec to, double maxMove)
{
  const Vec d = to - from; const double n = d.Length();
  return n > maxMove ? from + d * (maxMove / n) : to;
}
struct Sample { Pose pose; uint32_t tick=0; unsigned peer=0; unsigned slot=0; };

// Near the local car, match the native collision pose instead of showing a
// car farther ahead or driving a prediction through the local cockpit.
inline double ContactWeight(Vec local, Vec raw, Vec projected)
{
  if (!ValidPosition(local)) return 0;
  const Vec shift=projected-raw, fromLocal=raw-local;
  const double t=shift.Dot(shift)>1e-9 ? std::clamp(-fromLocal.Dot(shift)/shift.Dot(shift),0.0,1.0) : 0;
  const double w=std::clamp(((fromLocal+shift*t).Length()-7.0)/13.0,0.0,1.0);
  return w*w*(3.0-2.0*w);
}
inline Pose NearLocal(Pose native,Pose predicted,Vec local)
{
  const double w=ContactWeight(local,native.p,predicted.p);
  return {native.p+(predicted.p-native.p)*w,
          uint16_t(int(native.yaw)+int(std::lround(w*YawDelta(predicted.yaw,native.yaw))))};
}

// source tick comes from native TX +0x14 (not the packet-arrival time or lap
// timer). Render steps use the local guest frame. No synchronized wall clock
// or assumed equality of the cabinets' start ticks.
class Predictor
{
public:
  void Clear() { *this={}; }
  Pose Step(Sample s,uint64_t frame,double lead,unsigned smoothing)
  {
    const Pose raw=s.pose;
    if (!s.peer || s.peer>MaxSeats || s.slot>=MaxActors || !ValidPosition(raw.p)) { Clear(); return raw; }
    if (!m_valid || s.peer!=m_last.peer || s.slot!=m_last.slot || frame<m_frame || frame-m_frame>9)
      return Seed(s,frame);
    // A display list may request the same actor several times per vblank.
    if (frame==m_frame) return s.tick==m_last.tick && Same(raw,m_last.pose) ? m_shown : raw;
    const double step=double(frame-m_frame);
    const uint32_t dt=s.tick-m_last.tick;  // unsigned wrap; backwards ticks reset below
    const Vec delta=raw.p-m_last.pose.p;
    const int turn=YawDelta(raw.yaw,m_last.pose.yaw);
    const bool hadVelocity=m_velocity;
    const Vec priorSpeed=m_speed;
    const double priorTurn=m_turn;
    if (dt)
    {
      // Backwards tick or a move no car can make: a native reset/warp, snap.
      if (int32_t(dt)<0 || delta.Length()>3.5*dt) return Seed(s,frame);
      const Vec v=delta*(1.0/dt);
      const double w=double(turn)/dt;
      const bool hole=dt>8 || frame-m_sampleFrame>8;
      const bool abrupt=m_velocity && ((v-m_speed).Length()>0.95 || std::abs(w-m_turn)>420);
      // Warm-up with a hole or spin: nothing to glide from, snap to native.
      if (!m_velocity && (hole || std::abs(turn)>700*int(dt))) return Seed(s,frame);
      // Holes and abrupt changes keep the history and glide the display; the
      // speed estimate follows the new sample at once rather than staying stale.
      const double a=(!m_velocity || abrupt) ? 1.0 : 0.38;
      m_speed=m_speed+(v-m_speed)*a;
      m_turn+=(w-m_turn)*a;
      m_velocity=true;
      m_sampleFrame=frame;
      m_last=s;
    }
    else if (!Same(raw,m_last.pose)) return Seed(s,frame); // inconsistent row

    const double stale=double(frame-m_sampleFrame);
    if (!m_velocity) return Seed(s,frame);  // warm-up: no speed yet, snap to native
    // Stale samples do not reseed: the fade below eases the display to native.
    lead=std::isfinite(lead) ? std::clamp(lead,0.0,MaxLead) : 0.0;
    // Ramp in after a reset, but don't let momentary receive jitter pull the
    // display back and forth. Long-run lead only follows sustained changes.
    ++m_updates;
    const double leadError=lead-m_lead;
    if (m_updates<32)
      m_lead+=std::clamp(leadError,-0.55,0.55)*0.35;
    else if (std::abs(leadError)>0.16)
      m_lead+=std::clamp(leadError,-0.065,0.065)*0.10;
    // Coast across brief holes, then fade back to the latest native pose.
    const double fade=std::clamp((10.0-stale)/5.0,0.0,1.0);
    const double ahead=std::min(m_lead+stale,MaxCoast)*fade;
    const Vec goal=raw.p+m_speed*ahead;
    const double goalYaw=m_turn*ahead;
    const Vec carry=m_shown.p+(hadVelocity ? priorSpeed : m_speed)*(step*fade);
    const double e=(carry-goal).Length(), speed=m_speed.Length();
    const double small=std::max(0.12,speed*0.45);
    const double gentle=1.0-std::clamp((e-small)/(3.0*small),0.0,1.0);
    const double tau=1.65+(smoothing ? (smoothing==1 ? 3.35 : 5.35)*gentle : 0.0);
    const double blend=std::exp(-step/tau)*fade;
    Vec shift=goal+(carry-goal)*blend-raw.p;
    const double n=shift.Length();
    if (n>MaxShift) shift=shift*(MaxShift/n);
    const double carryYaw=YawDelta(m_shown.yaw,raw.yaw)+(hadVelocity ? priorTurn : m_turn)*step*fade;
    const int yawShift=int(std::lround(std::clamp(goalYaw+(carryYaw-goalYaw)*blend,
                                -double(MaxYawShift),double(MaxYawShift))));
    const Vec prevShown=m_shown.p;
    m_shown={LimitMove(prevShown,raw.p+shift,(MaxRate+1.5*m_speed.Length())*step),uint16_t(int(raw.yaw)+yawShift)};
    m_frame=frame;
    return m_shown;
  }
private:
  static bool Same(Pose a,Pose b) { return a.p.x==b.p.x && a.p.z==b.p.z && a.yaw==b.yaw; }
  Pose Seed(Sample s,uint64_t frame)
  {
    m_valid=true; m_velocity=false; m_frame=m_sampleFrame=frame;
    m_updates=0; m_last=s; m_shown=s.pose; m_speed={}; m_turn=m_lead=0;
    return m_shown;
  }
  bool m_valid=false,m_velocity=false;
  uint64_t m_frame=0,m_sampleFrame=0;
  uint64_t m_updates=0;
  Sample m_last;
  Pose m_shown;
  Vec m_speed;
  double m_turn=0,m_lead=0;
};
}
#endif
