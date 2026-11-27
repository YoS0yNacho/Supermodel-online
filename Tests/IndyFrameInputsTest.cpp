// SPDX-License-Identifier: GPL-3.0-or-later
#include "IndyFrameInputs.h"
#include <cassert>
#include <cstdio>
using namespace IndyFrameInputs;
int main() {
  std::vector<Segment> s;
  assert(Parse("0,12,24,200,255,0;1,0,30,128,100,10",s));
  assert(s.size()==2);
  assert(At(s,0,11).steer==128 && At(s,0,11).accel==255);
  assert(At(s,0,12).steer==200 && At(s,0,23).steer==200 && At(s,0,24).steer==128);
  assert(At(s,1,12).accel==100 && At(s,1,12).brake==10);
  const char *bad[]={"", "0,0,1,128,255,0;", "0,0,1,128,255,0,", "0,0,1,128,256,0",
                     "2,0,1,128,255,0", "0,1,1,128,255,0", "0,0,100001,128,255,0",
                     "0,-1,2,128,255,0", "0,0,10,128,255,0;0,9,11,128,255,0"};
  for (auto p:bad) { assert(!Parse(p,s)); assert(s.empty()); }
  assert(Parse("0,0,1,128,255,0;0,1,2,255,255,0",s));
  std::puts("PASS lab frame-input script: boundaries, actor mapping and validation");
}
