// SPDX-License-Identifier: GPL-3.0-or-later
// ROM-free tests for the experimental local collision inputs.
#include "Network/Games/Indy500/LocalCollision.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
using namespace IndyLocalCollision;
int main() {
  assert(ConfigAllowed(true,true,false));
  assert(!ConfigAllowed(false,true,false));assert(!ConfigAllowed(true,false,false));assert(!ConfigAllowed(true,true,true));
  assert(Active(true,true,true,false,true,2,0));
  assert(!Active(false,true,true,false,true,2,0));assert(!Active(true,false,true,false,true,2,0));
  assert(!Active(true,true,false,false,true,2,0));assert(!Active(true,true,true,true,true,2,0));
  assert(!Active(true,true,true,false,false,2,0));assert(!Active(true,true,true,false,true,3,0));
  assert(!Active(true,true,true,false,true,2,2));
  Input s;const uint32_t base=ActorBase;
  assert(!s.Resolve(0xac4e8,base+0x124,0,100,{10,20}).substituted); // warm up
  assert(!s.Resolve(0xac504,base+0x144,0,100,{15,25}).substituted);
  auto x=s.Resolve(0xac4e8,base+0x124,0,101,{11,22});assert(x.substituted && x.value==11);
  auto z=s.Resolve(0xac4fc,base+0x12c,0,101,{80,90});assert(z.substituted && z.value==22);
  assert(s.Resolve(0xac504,base+0x144,0,101,{80,90}).value==10);
  assert(s.Resolve(0xac510,base+0x148,0,101,{80,90}).value==20);
  assert(s.Resolve(0xac2ac,base+0x124,0,101,{80,90}).value==11);
  assert(s.Resolve(0xac2bc,base+0x12c,0,101,{80,90}).value==22);
  assert(s.Resolve(0xac2c4,base+0x144,0,101,{80,90}).value==10);
  assert(s.Resolve(0xac2cc,base+0x148,0,101,{80,90}).value==20);
  assert(!s.Resolve(0xac2cc,base+0x124,0,101,{80,90}).substituted);
  assert(!s.Resolve(0xac4e8,base+ActorStride+0x124,0,102,{12,23}).substituted); // peer
  assert(!s.Resolve(0xdead,base+0x124,0,102,{12,23}).substituted);
  x=s.Resolve(0xac4e8,base+0x124,0,102,{12,23});assert(x.substituted && x.value==12); // rejected reads didn't advance
  assert(!s.Resolve(0xac4e8,base+0x124,0,104,{13,23}).substituted); // gap
  assert(!s.Resolve(0xac4e8,base+0x124,0,103,{14,23}).substituted); // rewind
  assert(!s.Resolve(0xac4e8,base+0x124,0,104,{30,23}).substituted); // warp
  assert(s.Resolve(0xac4e8,base+0x124,0,105,{31,23}).substituted);
  assert(!s.Resolve(0xac4e8,base+ActorStride+0x124,1,106,{32,23}).substituted); // actor switch
  assert(s.Resolve(0xac4e8,base+ActorStride+0x124,1,107,{33,23}).substituted);
  assert(!s.Resolve(0xac4e8,base+ActorStride+0x124,1,108,{NAN,23}).substituted);
  assert(!s.Resolve(0xac4e8,base+ActorStride+0x124,1,109,{34,23}).substituted);
  assert(s.Resolve(0xac4e8,base+ActorStride+0x124,1,110,{35,23}).substituted);
  s.Clear();assert(!s.Resolve(0xac4e8,base+ActorStride+0x124,1,111,{36,23}).substituted);
  assert(!s.Resolve(0xac4e8,base+0x124,2,112,{1,2}).substituted);
  std::puts("PASS: local collision sites, latching and conservative fallbacks");
}
