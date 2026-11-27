// SPDX-License-Identifier: GPL-3.0-or-later
// c++ -std=c++17 -Wall -Wextra -pedantic -O2 Tests/IndyLocalCollisionHistoryTest.cpp -o /tmp/indy-history-test
#include "IndyLocalCollisionHistory.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

using namespace IndyLocalCollisionLab;
int main()
{
  History h;
  auto s=h.Step(10,0,{10,20});
  assert(s.valid && s.reset && s.now.x==10 && s.previous.x==10);
  s=h.Step(11,0,{11.5f,19});
  assert(s.valid && !s.reset && s.previous.x==10 && s.previous.z==20);
  s=h.Step(11,0,{99,99}); // latch same-frame x/z despite guest writes
  assert(!s.reset && s.now.x==11.5f && s.now.z==19 && s.previous.x==10);
  s=h.Step(12,1,{12,20}); // actor changed
  assert(s.reset && s.previous.x==12);
  s=h.Step(14,1,{13,20}); // missed frame
  assert(s.reset && s.previous.x==13);
  s=h.Step(13,1,{14,20}); // frame rewind
  assert(s.reset && s.previous.x==14);
  s=h.Step(14,1,{22,20}); // exactly max is accepted
  assert(!s.reset && s.previous.x==14);
  s=h.Step(15,1,{31,20}); // warp: no synthetic velocity
  assert(s.reset && s.previous.x==31);
  s=h.Step(16,1,{32,20});
  assert(!s.reset && s.previous.x==31);
  s=h.Step(17,1,{std::numeric_limits<float>::quiet_NaN(),0});
  assert(!s.valid);
  s=h.Step(18,1,{33,20});
  assert(s.valid && s.reset && s.previous.x==33);
  s=h.Step(19,1,{8192,0});
  assert(!s.valid);
  s=h.Step(20,1,{33,20});
  assert(s.reset);
  h.Clear();
  s=h.Step(21,1,{34,20});
  assert(s.reset && s.previous.x==34);
  std::puts("PASS local collision history: latch, progression, identity, gaps, rewind, warp, invalid and clear");
}
