// SPDX-License-Identifier: GPL-3.0-or-later
// Blend-time policy shared by the receive-side motion predictors.
//
// When a snapshot arrives, the difference between the pose being displayed and
// the new prediction target is blended out exponentially (time constant tau in
// emulated frames). With a fixed tau every small prediction error becomes a
// short, visible nudge: on a 150-250 ms relay that is the "micro rubber band"
// seen on remote cars. Small errors can be blended far more slowly without a
// perceptible cost, because the extra lag they add is a fraction of a frame of
// travel; large errors (real divergence, collisions, respawns) still have to
// close quickly. Level 0 keeps the historical fixed blend exactly.
#ifndef INCLUDED_NETWORK_CORRECTION_SMOOTHING_H
#define INCLUDED_NETWORK_CORRECTION_SMOOTHING_H
#include <algorithm>
#include <atomic>

namespace GameNetwork
{
// NetPredictionSmoothing: 0 = fixed blend (legacy), 1 = error-proportional
// (default), 2 = error-proportional, extra smooth. Process-wide preference,
// read on the emulation thread; set at start-up from the configuration.
constexpr unsigned DefaultSmoothingLevel = 1, MaxSmoothingLevel = 2;
inline std::atomic<unsigned>& SmoothingLevelStore()
{
  static std::atomic<unsigned> level{DefaultSmoothingLevel};
  return level;
}
inline unsigned SmoothingLevel() { return SmoothingLevelStore().load(std::memory_order_relaxed); }
inline void SetSmoothingLevel(unsigned level)
{
  SmoothingLevelStore().store(std::min(level, MaxSmoothingLevel), std::memory_order_relaxed);
}

// Extra blend time for distant relays. The legacy curve only started at 18
// frames of one-way sample age; levels 1-2 start at 8 (about 130 ms), so a
// 150-250 ms relay gets some of it.
inline double FarLatencyTau(double latencyFrames)
{
  const double start = SmoothingLevel() ? 8.0 : 18.0;
  return latencyFrames > start ? 0.35 * (latencyFrames - start) : 0.0;
}

// Stretch tau for corrections that are small relative to how far the car moves
// per frame. `errorDistance` is |displayed - new target|, `speed` is the car's
// speed in world units per frame, `unit` is the game's notion of a small
// distance (1/24 of its snap distance). Errors up to max(unit, 0.3 * speed)
// get the full stretch; the stretch fades out linearly by four times that.
// Below ~100 ms of sample age (6 frames) the plain blend is already clean, so
// the stretch ramps in between 7 and 11 frames of latency: no lag is added
// where there was nothing to smooth.
inline double ErrorScaledTau(double tau, double errorDistance, double speed, double unit,
                             double latencyFrames)
{
  const unsigned level = SmoothingLevel();
  if (!level) return tau;
  const double ramp = std::clamp((latencyFrames - 7.0) / 4.0, 0.0, 1.0);
  const double stretch = 1.0 + ((level == 1 ? 2.5 : 3.5) - 1.0) * ramp;
  if (stretch <= 1.0) return tau;
  const double small = std::max(unit, 0.3 * speed), large = 4.0 * small;
  if (errorDistance <= small) return tau * stretch;
  if (errorDistance >= large) return tau;
  const double t = (errorDistance - small) / (large - small);
  return tau * (stretch - (stretch - 1.0) * t);
}
}
#endif
