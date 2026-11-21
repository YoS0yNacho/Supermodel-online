// Sega Rally 2 pose handoff regression (no ROM or network required).
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Srally2Packet.h"
#include <cassert>
#include <cmath>
#include <cstdio>

using namespace Srally2;

static PoseSample Pose(unsigned f)
{
  PoseSample p;
  p.x = 0.82 * f;
  p.z = 0.31 * f;
  p.vx = 0.82;
  p.vz = 0.31;
  p.yaw = 3500;
  p.carTag = 0x20008000u;
  return p;
}

static void Equal(const PoseOutput& a, const PoseOutput& b)
{
  assert(std::abs(a.x - b.x) < 1e-4f && std::abs(a.y - b.y) < 1e-4f &&
         std::abs(a.z - b.z) < 1e-4f);
  assert(a.pitch == b.pitch && a.yaw == b.yaw && a.roll == b.roll);
}

static void CheckHandoff(double frameUs)
{
  const auto stamp = [&](unsigned f) { return 1'000'000ULL + uint64_t(std::llround(f * frameUs)); };
  PoseMotion ref, actual, jittered;
  PoseOutput a{}, b{}, c{};
  for (unsigned f = 1; f <= 70; ++f)
  {
    const bool ready = ref.Apply(Pose(f), 0, stamp(f), f, 1.8, 0.14, a, frameUs);
    assert(actual.Apply(Pose(f), 0, stamp(f), f, 1.8, 0.14, b, frameUs) == ready);
    assert(jittered.Apply(Pose(f), 0, stamp(f) + (f % 2u ? 1400u : 0u), f, 1.8, 0.14, c, frameUs) == ready);
    if (ready) { Equal(a,b); Equal(a,c); }
  }
  const auto last = b;
  // TCP comes in two source frames behind where UDP was, though its control
  // arrived in order. This is equivalent to one more frame of the last pose.
  assert(ref.Apply(Pose(70), 0, stamp(70), 71, 2.8, 0.14, a, frameUs));
  assert(actual.Apply(Pose(68), 0, stamp(68), 71, 4.8, 0.14, b, frameUs));
  Equal(a,b);
  assert(b.x > last.x + 0.5f);
  // A returning TCP copy of the same input has a slightly later send stamp.
  // It is not a new native frame with zero motion.
  assert(ref.Apply(Pose(70), 0, stamp(70), 72, 3.8, 0.14, a, frameUs));
  assert(actual.Apply(Pose(70), 0, stamp(70)+120, 72, 2.8, 0.31, b, frameUs));
  Equal(a,b);
  assert(ref.Apply(Pose(73), 0, stamp(73), 73, 1.8, 0.14, a, frameUs));
  assert(actual.Apply(Pose(73), 0, stamp(73), 73, 1.8, 0.14, b, frameUs));
  Equal(a,b);
  // Do not mask a real reset/warp or let the newer pose coast indefinitely.
  auto warp = Pose(74);
  warp.x += 210.0;
  assert(!ref.Apply(warp, 0, stamp(74), 74, 1.8, 0.14, a, frameUs));
  assert(!actual.Apply(Pose(69), 0, stamp(69), 200, 4.8, 0.14, b, frameUs));
}

static void CheckControl()
{
  Predictor p;
  for (unsigned f = 1; f < 25; ++f)
  {
    auto packet = MakeSrally2RacePacket(1,0,float(f)*0.8f,2.0f,7.0f,0.8f,0,0,0,3500,0,0,2u);
    // Live status bits repeatedly change without a new car or position reset.
    const auto tag=Read32(packet.data(),HumanPoseOffset+0x44);
    Write32(packet.data(),HumanPoseOffset+0x44,tag ^ (f%2u ? 0x00004000u : 0x00010000u));
    auto out = packet;
    const auto result = p.Apply(packet.data(),packet.size(),out.data(),f,1.0,0.1,1000000+17384*f,1,0,2,17384.0);
    assert(result.human == (f > 1));
    for (size_t o=0; o<out.size(); ++o)
    {
      const size_t address=o^2;
      const auto editable=[address](size_t base) {
        const size_t relative=address-base;
        return relative < 0x0c || (relative >= 0x18 && relative < 0x1e);
      };
      if (!editable(HumanPoseOffset) && !editable(CpuPoseBase) && !editable(CpuPoseBase+PoseStride))
        assert(packet[o] == out[o]);
    }
  }
  // A real pose identity change still requires a new position seed, even
  // when it arrives with an older stamp than the last accepted pose.
  auto replace = MakeSrally2RacePacket(1,0,20,2,7,0.8f,0,0,0,3500,0,0,2u);
  const size_t tagOffset = HumanPoseOffset + 0x44;
  const uint32_t newTag = Read32(replace.data(),tagOffset) ^ 0x00040000u;
  Write32(replace.data(),tagOffset,newTag);
  auto out = replace;
  assert(!p.Apply(replace.data(),replace.size(),out.data(),25,1.0,0.1,1400000,1,0,2,17384.0).human);

  auto next = MakeSrally2RacePacket(1,0,20.8f,2,7,0.8f,0,0,0,3500,0,0,2u);
  Write32(next.data(),tagOffset,newTag);
  out = next;
  assert(p.Apply(next.data(),next.size(),out.data(),26,1.0,0.1,1417384,1,0,2,17384.0).human);

  // Change only the native phase. A continuous sample would keep predicting
  // for this short rewind, so the lack of output isolates the phase reset.
  auto phase = next;
  Write32(phase.data(),0x18,11u);
  out = phase;
  assert(!p.Apply(phase.data(),phase.size(),out.data(),27,1.0,0.1,1400000,1,0,2,17384.0).human);
  assert(phase == out);
}

int main()
{
  CheckHandoff(1000000.0 / 57.524);
  CheckHandoff(1000000.0 / 60.0);
  CheckControl();
  std::puts("PASS Rally motion: late TCP, duplicate UDP/TCP pose, native cadence, status-bit transitions, finite hold, warp and phase reset");
}
