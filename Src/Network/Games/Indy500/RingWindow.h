// INDY 500 Online: bounded startup credits for the native data/FC ring.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_INDY500_RING_WINDOW_H
#define INCLUDED_INDY500_RING_WINDOW_H

#include <cstdint>

namespace IndyRing
{
// The native board gets at most one new pair per guest frame. Default 57.524 Hz.
constexpr uint64_t FrameUs = 17384;
constexpr unsigned MaxSeedPairs = 32;
constexpr unsigned JitterFrames = 2;

// The native master retries FF each vblank while waiting. The first returned
// FF corresponds to the first send, not to the most recent retry.
constexpr uint64_t FirstFfStamp(uint64_t previous, uint64_t now)
{
  return previous ? previous : now;
}

// The master's native FF handshake came back through every seat. Use that
// same-room round time to estimate how long its first complete data/FC pair
// can be in flight. Keep the old small window when it already covers the trip.
constexpr unsigned SeedLimit(unsigned seats, uint64_t roundUs, uint64_t frameUs = FrameUs)
{
  if (seats < 2 || seats > 8 || !frameUs) return 0; // only supported native rosters
  const uint64_t frames = roundUs / frameUs + (roundUs % frameUs != 0);
  if (frames <= seats) return seats;
  return frames >= MaxSeedPairs - JitterFrames ? MaxSeedPairs : unsigned(frames) + JitterFrames;
}

// Startup credits end as soon as the first returned pair can be applied
// (after the original minimum). Never create extra credits in steady state.
constexpr bool ShouldSeed(unsigned seats, unsigned limit, unsigned sent, bool primed, bool hasCompletePair)
{
  return !primed && sent < limit && (sent < seats || !hasCompletePair);
}
}

#endif
