// SPDX-License-Identifier: GPL-3.0-or-later
// Presentation-only predictor: multiple snapshots drawn within one frame.
#include "Network/Games/Indy500/Motion.h"
#include <cassert>
#include <cmath>
#include <cstdio>
using namespace IndyMotion;
static Sample S(uint32_t tick,double x,uint16_t yaw=0) {return {{{x,0},yaw},tick,2,0};}
static void Near(double x,double y) {assert(std::abs(x-y)<1e-7);}
int main() {
  Predictor p;
  Pose before{};
  for(unsigned frame=0;frame<80;++frame) before=p.Step(S(frame,frame),frame,5,1);
  assert(before.p.x>80); // predictor is ahead, not still warming up
  const auto advanced=p.Step(S(80,80),79,5,1);
  Near(advanced.p.x,before.p.x); // no snap back to the newly received raw pose
  Near(advanced.p.z,before.p.z);
  assert(advanced.yaw==before.yaw);
  const auto advancedAgain=p.Step(S(81,81),79,5,1);
  Near(advancedAgain.p.x,before.p.x); // no repeated step or history update
  const auto next=p.Step(S(82,82),80,5,1);
  assert(next.p.x>=advanced.p.x && (next.p-advanced.p).Length()<=MaxRate+1.5*3.5);
  assert((advanced.p-S(80,80).pose.p).Length()<=MaxShift);

  p.Clear();for(unsigned f=0;f<50;++f) p.Step(S(f,f,65520),f,5,1);
  auto warp=p.Step(S(50,500),49,5,1);
  Near(warp.p.x,500); // same-frame warp must not retain the old pose
  auto mismatch=p.Step(S(50,501),49,5,1);
  Near(mismatch.p.x,501); // same tick with changed data is not interpolated
  auto back=p.Step(S(49,499),49,5,1);
  Near(back.p.x,499); // backwards tick resets
  p.Clear();
  for(unsigned f=0;f<40;++f) before=p.Step(S(UINT32_MAX-20+f,f,uint16_t(65500+f)),f,4,1);
  auto wrap=p.Step(S(UINT32_MAX-20+40,40,uint16_t(4)),39,4,1);
  assert((wrap.p-before.p).Length()<1e-7);
  assert(std::abs(YawDelta(wrap.yaw,4))<=MaxYawShift);
  std::puts("PASS same-frame newer samples, repeat, bounded continuity, resets and tick wrap");
}
