// SPDX-License-Identifier: GPL-3.0-or-later
// EXPERIMENTAL Indy 500 Twin Rev A local collision input.
// This does not synchronize contact outcomes or modify remote/network state.
#ifndef INCLUDED_INDY500_LOCAL_COLLISION_H
#define INCLUDED_INDY500_LOCAL_COLLISION_H
#include <cmath>
#include <cstdint>

namespace IndyLocalCollision {
struct Vec { float x=0, z=0; };
struct Read { bool substituted=false; float value=0; };
constexpr uint32_t ActorBase=0x53c7a0, ActorStride=0x1e4;
// Provisional conservative discontinuity bound, NOT a validated teleport rule.
constexpr float MaxStep=8.0f;
inline bool Valid(Vec v) {
  return std::isfinite(v.x) && std::isfinite(v.z) && std::abs(v.x)<8192 && std::abs(v.z)<8192;
}
inline bool ConfigAllowed(bool requested,bool verifiedRom,bool conflictingLab) {
  return requested && verifiedRom && !conflictingLab;
}
inline bool Active(bool armed,bool online,bool role,bool failed,bool race,unsigned count,unsigned actor) {
  return armed && online && role && !failed && race && count==2 && actor<2;
}

class Input {
public:
  void Clear() { *this={}; }
  // No mutation except for an exact local actor/read-site match. A frame is
  // usable only after two consecutive, valid samples. Discontinuities fall
  // back to the untouched native read for the entire frame. Latch once per
  // emulator frame so the current/history X/Z values come from one sample.
  Read Resolve(uint32_t pc,uint32_t address,unsigned actor,uint64_t frame,Vec now) {
    if(actor>=2) { Clear(); return {}; }
    bool x=false,previous=false;
    switch(pc) {
      case 0xac4e8: case 0xac2ac: x=true; break;
      case 0xac4fc: case 0xac2bc: break;
      case 0xac504: case 0xac2c4: x=true;previous=true;break;
      case 0xac510: case 0xac2cc: previous=true;break;
      default: return {};
    }
    const uint32_t field=previous ? (x?0x144:0x148) : (x?0x124:0x12c);
    if(address!=ActorBase+actor*ActorStride+field) return {};
    if(!m_valid || actor!=m_actor || frame!=m_frame) {
      if(!Valid(now)) {Clear();return {};}
      const float dx=now.x-m_now.x,dz=now.z-m_now.z;
      const bool continuous=m_valid && actor==m_actor && frame>m_frame && frame-m_frame==1 &&
                            dx*dx+dz*dz<=MaxStep*MaxStep;
      m_previous=continuous?m_now:now;
      m_now=now;m_frame=frame;m_actor=actor;m_valid=true;m_usable=continuous;
    }
    if(!m_usable) return {};
    const Vec p=previous?m_previous:m_now;
    return {true,x?p.x:p.z};
  }
private:
  Vec m_now{},m_previous{};
  uint64_t m_frame=0;
  unsigned m_actor=0;
  bool m_valid=false,m_usable=false;
};
}
#endif
