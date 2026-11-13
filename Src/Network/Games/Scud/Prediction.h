// Scud-specific receive-copy prediction. SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_SCUD_PREDICTION_H
#define INCLUDED_SCUD_PREDICTION_H
#include "Policy.h"
#include "../CorrectionSmoothing.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <vector>
namespace Scud {
// PowerPC offsets are big-endian; HLE wire words swap 16-bit halves.
inline uint8_t Read8(const uint8_t* p, size_t o) { return p[o^2]; }
inline uint16_t Read16(const uint8_t* p, size_t o) { return (uint16_t(Read8(p,o))<<8)|Read8(p,o+1); }
inline uint32_t Read32(const uint8_t* p, size_t o) { return (uint32_t(Read16(p,o))<<16)|Read16(p,o+2); }
inline void Write16(uint8_t* p, size_t o, uint16_t v) { p[o^2]=uint8_t(v>>8); p[(o+1)^2]=uint8_t(v); }
inline void Write32(uint8_t* p, size_t o, uint32_t v) { Write16(p,o,uint16_t(v>>16)); Write16(p,o+2,uint16_t(v)); }
inline float ReadFloat(const uint8_t* p, size_t o) { uint32_t v=Read32(p,o); float f; std::memcpy(&f,&v,4); return f; }
inline void WriteFloat(uint8_t* p, size_t o, float f) { uint32_t v; std::memcpy(&v,&f,4); Write32(p,o,v); }
static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559,"Scud binary32");
// Native packet header carries the human count, owner map and race authority.
struct Routing {
  uint8_t local=0, machines=0, humans=0, authority=0;
  unsigned total=0;
  std::array<uint8_t,8> players{};
  std::array<bool,MaxCars> cpu{};
  bool valid=false;
  uint8_t fail=0;  // which validation step rejected the snapshot (0 = accepted)
};
// Stages 19–21 cover rolling start through green; prediction starts before GO.
inline bool InRacePhase(unsigned phase) { return phase >= 19 && phase <= 21; }
inline bool DecodeHeader(const uint8_t* p, size_t size, uint32_t& tick) {
  if (!p || size!=PacketSize || Read8(p,0x1f)!=Read8(p,0x21) ||
      !InRacePhase(Read8(p,0x1f))) return false;
  uint32_t sum=0;
  for (unsigned i=4; i<0x40; i+=4) sum+=Read32(p,i);
  if (sum!=Read32(p,0x40)) return false;
  tick=Read32(p,0x38);
  return true;
}
// Bounded receive-copy prediction; native clocks and ownership remain untouched.
template<unsigned N, bool Progress=false> class Motion {
  using Point=std::array<double,N>;
  double intervalFrames=1.0, jitterFrames=0.0, latencyFrames=0.0, activeTau=6.0;
  bool seeded=false, moving=false;
  uint32_t tick=0, lastDt=1; uint64_t received=0; double age=0, omega=0;
  Point value{}, chordVel{}, velocity{}, correction{}, velCorrection{};
  static double Norm(const Point& a) { double s=0; for(double x:a) s+=x*x; return std::sqrt(s); }
  bool SmoothMode() const { return !Progress && (intervalFrames>1.001 || jitterFrames>0.25); }
  bool LatencyExtended() const { return !Progress && latencyFrames>12.0; }
  double MaxFrames() const {
    if (Progress) return intervalFrames<=1.001 ? 12.0 : std::min(36.0, 12.0 + 1.8*(intervalFrames-1.0));
    if (!SmoothMode()) return LatencyExtended() ? std::min(36.0, latencyFrames*1.25) : 12.0;
    // Extend low-rate prediction by measured one-way sample age.
    const double base = std::min(42.0, 12.0 + 1.8*(intervalFrames-1.0) + 1.8*jitterFrames + (jitterFrames>0.25 ? 10.0 : 0.0));
    return std::max(base, LatencyExtended() ? std::min(36.0, latencyFrames*1.25) : 0.0);
  }
  double Limit() const {
    if (Progress) return intervalFrames<=1.001 ? 4.0 : std::min(24.0, 4.0 + 0.8*(intervalFrames-1.0));
    if (!SmoothMode()) return LatencyExtended() ? std::max(24.0, MaxFrames()*5.0) : 24.0;
    return std::max(24.0, MaxFrames()*5.0);
  }
  double Snap(uint32_t dt=1) const {
    if (Progress) return Limit();
    if (!SmoothMode()) return 24.0;
    return 24.0 + 3.5*(intervalFrames-1.0) + (jitterFrames>0.25 ? 2.2*double(std::min(dt, 45u)) : 0.0);
  }
  double Tau() const { return std::max(6.0, intervalFrames*1.25 + (Progress ? 0.0 : 0.35*jitterFrames) + (LatencyExtended() ? 0.15*(latencyFrames-12.0) : 0.0) + (!Progress ? GameNetwork::FarLatencyTau(latencyFrames) : 0.0)); }
  static double SoftCap(double x, double cap, double knee=2.0) {
    if (x<=0.0) return 0.0;
    const double th=std::max(0.0, cap-knee);
    return (x<=th || knee<=1e-6) ? std::min(x,cap) : (cap - knee*std::exp(-(x-th)/knee));
  }
  static Point Rotate2(const Point& v, double a) {
    Point r=v;
    if constexpr (N==2 && !Progress) {
      const double c=std::cos(a), s=std::sin(a);
      r[0]=v[0]*c - v[1]*s; r[1]=v[0]*s + v[1]*c;
    }
    return r;
  }
  Point Lead(Point v, double frames) const {
    if (frames<=0.0) { Point z{}; return z; }
    Point disp=v;
    if constexpr (N==2 && !Progress) {
      if (SmoothMode() && std::abs(omega)>1e-6) {
        // Preserve turning arcs and damp long, low-confidence projections.
        const bool farRelay = LatencyExtended();
        const double freeH = farRelay ? std::min(24.0, intervalFrames + 6.0 + (latencyFrames - 12.0) * 0.5) : std::min(16.0, intervalFrames + 6.0);
        const double dampScale=(farRelay ? 8.0 : 6.0)*std::max(6.0, intervalFrames + 0.5*jitterFrames);
        const double effOmega=(frames<=freeH) ? omega
            : omega*std::exp(-(frames-freeH)/dampScale);
        const double half=0.5*effOmega*frames;
        if (std::abs(half)>=1e-4) {
          disp=Rotate2(v, half);
          const double scale=std::sin(half)/(0.5*effOmega);
          disp[0]*=scale; disp[1]*=scale;
          const double len=Norm(disp), lim=Limit();
          if (len>lim) { disp[0]*=lim/len; disp[1]*=lim/len; }
          return disp;
        }
      }
    }
    for (double& x:disp) x*=frames;
    const double len=Norm(disp), limit=Limit();
    if (len>limit) for(double& x:disp) x*=limit/len;
    return disp;
  }
  Point BaseTarget(double elapsed) const {
    const double mf = MaxFrames();
    const double knee = (!Progress && jitterFrames>0.25) ? std::min(8.0, 0.25*mf) : 2.0;
    const double frames = (!SmoothMode() && intervalFrames<=1.001)
        ? std::min(MaxFrames(), age+elapsed)
        : SoftCap(age+elapsed, mf, knee);
    Point p=Lead(velocity, frames);
    for(unsigned i=0;i<N;++i) p[i]+=value[i];
    return p;
  }
  Point EvaluateAtElapsed(double elapsed) const {
    const double tau=activeTau;
    const double e=std::exp(-elapsed/tau);
    const double vScale = SmoothMode() ? (2.5*(1.0-std::exp(-elapsed*0.4))*e) : 0.0;
    Point p=BaseTarget(elapsed);
    for(unsigned i=0;i<N;++i) p[i]+=correction[i]*e + velCorrection[i]*vScale;
    return p;
  }
  Point EvaluateVelocity(uint64_t frame) const {
    const double elapsed=double(frame-received);
    constexpr double eps=1e-3;
    Point a=EvaluateAtElapsed(elapsed+eps), b=EvaluateAtElapsed(elapsed>=eps ? elapsed-eps : elapsed);
    const double inv=elapsed>=eps ? (0.5/eps) : (1.0/eps);
    Point v{}; for(unsigned i=0;i<N;++i) v[i]=(a[i]-b[i])*inv;
    return v;
  }
  Point Evaluate(uint64_t frame) const {
    return EvaluateAtElapsed(double(frame-received));
  }
  void Seed(Point p,uint32_t t,uint64_t frame,double a) {
    const double kj=jitterFrames, kl=latencyFrames;
    Reset(); jitterFrames=kj; latencyFrames=kl;
    seeded=true; value=p; tick=t; received=frame; age=std::clamp(a,0.0,MaxFrames());
  }
public:
  void SetTickRate(unsigned hz) {
    hz=std::clamp(hz,5u,60u);
    intervalFrames=60.0/double(hz);
  }
  void Reset() {
    const double iv=intervalFrames;
    *this=Motion();
    intervalFrames=iv;
  }
  bool Apply(Point p,uint32_t t,uint64_t frame,double a,Point& output) {
    if (!std::isfinite(a) || a<0) { Reset();return false; }
    for(double x:p) if (!std::isfinite(x) || std::abs(x)>(Progress?32767:10000) || (Progress && x<0)) { Reset();return false; }
    if (!seeded || frame<received) { Seed(p,t,frame,a);return false; }
    const uint32_t dt=t-tick;
    if (!dt) {
      if (p!=value) { Seed(p,t,frame,a);return false; }
    } else {
      // Save the displayed pose before updating smoothing state.
      Point old=Evaluate(frame);
      Point oldVel=moving ? EvaluateVelocity(frame) : Point{};
      const double localGap = double(frame - received);
      const double prevJitter = jitterFrames;
      if constexpr (!Progress) {
        const double arrJit = std::abs(localGap - double(dt));
        if (arrJit > jitterFrames) jitterFrames += 0.65 * (arrJit - jitterFrames);
        else jitterFrames *= 0.96;
        jitterFrames = std::min(jitterFrames, 24.0);
        // Track sustained sample age separately from arrival jitter.
        if (a > latencyFrames) latencyFrames += 0.5 * (a - latencyFrames);
        else latencyFrames += 0.02 * (a - latencyFrames);
      }
      Point rawV{},error{};
      const Point expected=Lead(velocity, double(dt));
      for(unsigned i=0;i<N;++i) { rawV[i]=(p[i]-value[i])/double(dt); error[i]=p[i]-value[i]-expected[i]; }
      // Reject native progress reversals and wraparound.
      if (dt>120 || Norm(rawV)>(Progress?1.0:8.0) || (Progress && p[0]<value[0]) ||
          (moving && Norm(error)>Snap(dt))) { jitterFrames=prevJitter; Seed(p,t,frame,a);return false; }
      const bool smooth = SmoothMode();
      if (!moving) oldVel = rawV;
      if constexpr (N==2 && !Progress) {
        if (smooth && moving) {
          const double s0=Norm(chordVel), s1=Norm(rawV);
          double newOmega=0.0;
          if (s0>0.15 && s1>0.15) {
            const double cross=chordVel[0]*rawV[1]-chordVel[1]*rawV[0];
            const double dot=chordVel[0]*rawV[0]+chordVel[1]*rawV[1];
            const double dtheta=std::atan2(cross,dot);
            if (std::abs(dtheta)<1.0)
              newOmega=std::clamp(dtheta/std::max(1.0,0.5*double(lastDt+dt)), -0.045, 0.045);
          }
          const double blend=dt<=2 ? 0.20 : 0.50;
          omega=(1.0-blend)*omega + blend*newOmega;
        } else {
          omega=0.0;
        }
      }
      Point sampleVel=rawV;
      if constexpr (N==2 && !Progress) {
        if (smooth) sampleVel=Rotate2(rawV, 0.5*omega*double(dt));
      }
      Point v=sampleVel;
      if (moving && (Progress || dt<=2 || smooth)) {
        const double keep=Progress ? 0.65 : (dt<=2 ? 0.55 : 0.20);
        for(unsigned i=0;i<N;++i) v[i]=keep*velocity[i]+(1.0-keep)*sampleVel[i];
      }
      chordVel=rawV; lastDt=dt;
      const double maxF=MaxFrames();
      const double clampedAge=std::min(a,maxF);
      double nextAge=clampedAge;
      if (moving) {
        if (smooth) {
          const double kinAge=std::clamp(age + (localGap - double(dt)), 0.0, maxF);
          nextAge=(a>0.0) ? (kinAge + 0.20*(clampedAge - kinAge)) : kinAge;
        } else {
          nextAge=age + 0.25*(clampedAge - age);
        }
      }
      const bool wasMoving=moving;
      value=p;velocity=v;tick=t;received=frame;age=nextAge;moving=true;
      correction={}; velCorrection={};
      const Point newBase=BaseTarget(0.0);
      for(unsigned i=0;i<N;++i) correction[i]=old[i]-newBase[i];
      activeTau=Tau();
      if constexpr (N==2 && !Progress) {
        const double spd=Norm(velocity);
        // Small corrections blend slowly (no visible nudge); large ones keep the fast blend.
        activeTau=GameNetwork::ErrorScaledTau(activeTau, Norm(correction), spd, 1.0, latencyFrames);
        if (smooth && wasMoving && spd>0.2) {
          const double ahead=(correction[0]*velocity[0] + correction[1]*velocity[1]) / spd;
          if (ahead>0.0)
            activeTau=std::min(36.0, std::max(activeTau, ahead/(0.35*spd)));
        }
        if (smooth && wasMoving) {
          const Point newBaseVel=EvaluateVelocity(frame);
          for(unsigned i=0;i<N;++i) velCorrection[i]=(oldVel[i]-newBaseVel[i])*0.85;
        }
      }
    }
    if (!moving) return false;
    output=Evaluate(frame);
    // Bound correction displacement to the prediction limit.
    Point delta{}; for(unsigned i=0;i<N;++i) delta[i]=output[i]-value[i];
    double len=Norm(delta),limit=Limit();
    if(len>limit) for(unsigned i=0;i<N;++i) output[i]=value[i]+delta[i]*limit/len;
    if(Progress) output[0]=std::max(0.0,std::min(32767.0,output[0]));
    return true;
  }
};
struct Result { bool human=false; unsigned cpus=0; };
class Predictor {
  unsigned tickRateHz=60;
  Motion<2> human;
  std::array<Motion<1,true>,MaxCars> cpus;
  Routing previous;
  uint8_t humanId=255;
public:
  void SetTickRate(unsigned hz) {
    tickRateHz=std::clamp(hz,5u,60u);
    human.SetTickRate(tickRateHz);
    for(auto& c:cpus) c.SetTickRate(tickRateHz);
  }
  void Reset() {
    const unsigned hz=tickRateHz;
    *this=Predictor();
    SetTickRate(hz);
  }
  Result Apply(const uint8_t* raw,size_t size,uint8_t* out,uint64_t frame,double age,
               const Routing& route,unsigned owner) {
    Result result; uint32_t tick;
    if (!out || !route.valid || owner<1 || owner>route.machines || owner==route.local ||
        !DecodeHeader(raw,size,tick)) { Reset();return result; }
    if (route.local!=previous.local || route.machines!=previous.machines || route.humans!=previous.humans ||
        route.authority!=previous.authority || route.players!=previous.players || route.cpu!=previous.cpu) Reset();
    previous=route;
    const unsigned id=Read8(raw,0x48);
    // Validate the native owner map before predicting a human or CPU car.
    if (id>=route.humans || route.players[id]!=owner || Read8(raw,0x1c)!=route.authority || !(Read32(raw,0x44)&1)) {
      Reset();return result;
    }
    if (humanId!=id) { human.Reset();humanId=uint8_t(id); }
    std::array<double,2> position{{ReadFloat(raw,0x60),ReadFloat(raw,0x68)}},display;
    if (human.Apply(position,tick,frame,age,display)) {
      WriteFloat(out,0x60,float(display[0]));WriteFloat(out,0x68,float(display[1]));result.human=true;
    }
    // Select CPU progress from the native object map, not display color.
    const bool authority=owner==route.authority && owner!=route.local && Read8(raw,0x1c)==owner;
    for(unsigned i=0;i<MaxCars;++i) {
      const unsigned offset=0x124+i*4;
      if (!authority || !route.cpu[i] || !(Read16(raw,offset)&1)) { cpus[i].Reset();continue; }
      const int value=int16_t(Read16(raw,offset+2)); std::array<double,1> target;
      if (cpus[i].Apply({{double(value)}},tick,frame,age,target)) {
        Write16(out,offset+2,uint16_t(std::lround(target[0])));++result.cpus;
      }
    }
    return result;
  }
};
struct State {
  unsigned tickRateHz=60;
  Routing routing;
  std::vector<Predictor> predictors;
  bool humanLogged=false,cpuLogged=false,routingLogged=false;
  unsigned lastPhase=~0u;  // last packet stage logged, for the phase trace
  void SetTickRate(unsigned hz) {
    tickRateHz=std::clamp(hz,5u,60u);
    for(auto& p:predictors) p.SetTickRate(tickRateHz);
  }
  void Reset() {
    const unsigned hz=tickRateHz;
    *this=State();
    tickRateHz=hz;
  }
};
}
#endif
