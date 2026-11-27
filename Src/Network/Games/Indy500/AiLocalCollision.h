// SPDX-License-Identifier: GPL-3.0-or-later
// EXPERIMENTAL Indy 500 Rev A: local AI self-pose in AI-vs-AI detection.
#ifndef INCLUDED_INDY500_AI_LOCAL_COLLISION_H
#define INCLUDED_INDY500_AI_LOCAL_COLLISION_H
#include "LocalCollision.h"
#include <array>
#include <cstdint>

namespace IndyAiLocalCollision {
using IndyLocalCollision::Vec;
using IndyLocalCollision::Read;
constexpr unsigned Actors=16;
constexpr uint32_t LocalOwner=0xffffffffu;

struct Actor {
  uint32_t base=0;
  unsigned index=Actors, identity=Actors, owner=0, state=0;
  Vec live{};
};
struct Site { bool valid=false, candidate=false, previous=false, x=false; uint32_t field=0; };
inline Site Decode(uint32_t pc) {
  switch(pc) {
    case 0xa9790: return {true,true,false,true,0x124};
    case 0xa9794: return {true,true,false,false,0x12c};
    case 0xa9798: return {true,true,true,true,0x144};
    case 0xa979c: return {true,true,true,false,0x148};
    case 0xa97a0: return {true,false,false,true,0x124};
    case 0xa97a4: return {true,false,false,false,0x12c};
    case 0xa97a8: return {true,false,true,true,0x144};
    case 0xa97ac: return {true,false,true,false,0x148};
    default: return {};
  }
}
inline bool ValidActor(const Actor& a,unsigned local) {
  if(local>=2 || a.index<2 || a.index>=Actors || a.identity!=a.index ||
     a.base!=IndyLocalCollision::ActorBase+a.index*IndyLocalCollision::ActorStride ||
     (a.state&15)!=1) return false;
  const bool owned=a.index%2==local;
  return owned ? a.owner==LocalOwner : a.owner==1-local;
}
class Inputs {
public:
  void Clear() { *this={}; }
  Read Resolve(uint32_t pc,uint32_t addr,unsigned local,const Actor& subject,
               const Actor& candidate,uint64_t frame) {
    const auto site=Decode(pc);
    if(!site.valid) return {};
    const auto &target=site.candidate ? candidate : subject;
    if(addr!=target.base+site.field) return {};
    if(!ValidActor(subject,local) || !ValidActor(candidate,local) ||
       subject.index==candidate.index || subject.owner!=LocalOwner) {
      if(subject.index<Actors) m_history[subject.index]={};
      if(candidate.index<Actors) m_history[candidate.index]={};
      return {};
    }
    // Do not mix a live local pose with a delayed remote pose. The local
    // experiment is restricted to AI pairs simulated by this same cabinet.
    if(candidate.owner!=LocalOwner) return {};
    // Advance both histories together. If either actor lacks a continuous
    // local sample, keep the entire pair on native coordinates for this frame.
    auto &a=m_history[subject.index], &b=m_history[candidate.index];
    Advance(a,subject.live,frame);
    Advance(b,candidate.live,frame);
    if(!a.usable || !b.usable) return {};
    const auto &h=site.candidate ? b : a;
    const Vec p=site.previous?h.previous:h.now;
    return {true,site.x?p.x:p.z};
  }
private:
  struct History { Vec now{},previous{};uint64_t frame=0;bool valid=false,usable=false; };
  static void Advance(History& h,Vec live,uint64_t frame) {
    if(h.valid && frame==h.frame) return;
    if(!IndyLocalCollision::Valid(live)) { h={}; return; }
    const float dx=live.x-h.now.x,dz=live.z-h.now.z;
    const bool continuous=h.valid && frame>h.frame && frame-h.frame==1 &&
      dx*dx+dz*dz<=IndyLocalCollision::MaxStep*IndyLocalCollision::MaxStep;
    h.previous=continuous?h.now:live;
    h.now=live;h.frame=frame;h.valid=true;h.usable=continuous;
  }
  std::array<History,Actors> m_history{};
};
}
#endif
