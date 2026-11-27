// SPDX-License-Identifier: GPL-3.0-or-later
// EXPERIMENTAL lab helper: local pose history for Indy collision reads.
// Not integrated into the production emulator. The warp threshold is unvalidated.
#ifndef INCLUDED_TEST_INDY_LOCAL_COLLISION_HISTORY_H
#define INCLUDED_TEST_INDY_LOCAL_COLLISION_HISTORY_H

#include <cmath>
#include <cstdint>

namespace IndyLocalCollisionLab
{
struct Vec { float x = 0, z = 0; };
struct Sample { Vec now, previous; bool valid = false; bool reset = false; };
constexpr float MaxStep = 8.0f; // provisional units / frame; not a gameplay claim

inline bool Valid(Vec p)
{
  return std::isfinite(p.x) && std::isfinite(p.z) &&
         std::abs(p.x) < 8192.0f && std::abs(p.z) < 8192.0f;
}

class History
{
public:
  void Clear() { *this = {}; }

  // One snapshot per collision frame. Repeated calls in a frame are latched,
  // preventing x, z, and history reads from observing different local poses.
  // A first sample, actor change, frame discontinuity or large step seeds zero
  // velocity. Invalid coordinates fail closed and clear the previous sample.
  Sample Step(uint64_t frame, unsigned actor, Vec now)
  {
    if (!Valid(now)) { Clear(); return {}; }
    if (m_valid && actor == m_actor && frame == m_frame)
      return {m_now, m_previous, true, m_reset};
    const float dx = now.x-m_now.x, dz = now.z-m_now.z;
    const bool continuous = m_valid && actor == m_actor && frame > m_frame &&
                            frame-m_frame == 1 && dx*dx + dz*dz <= MaxStep*MaxStep;
    m_previous = continuous ? m_now : now;
    m_now = now;
    m_frame = frame;
    m_actor = actor;
    m_reset = !continuous;
    m_valid = true;
    return {m_now, m_previous, true, m_reset};
  }
private:
  uint64_t m_frame = 0;
  unsigned m_actor = 0;
  Vec m_now{}, m_previous{};
  bool m_valid = false, m_reset = false;
};
}
#endif
