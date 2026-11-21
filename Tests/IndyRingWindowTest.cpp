// INDY 500 multi-cabinet startup credits (ROM-free regression).
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Network/Games/Indy500/RingWindow.h"
#include <cassert>
#include <cstdio>
#include <limits>

using namespace IndyRing;
static_assert(SeedLimit(2, 350000) == 0 && SeedLimit(9, 350000) == 0);
static_assert(SeedLimit(3, 0) == 3 && SeedLimit(8, 8 * FrameUs) == 8);
static_assert(SeedLimit(3, 342000) == 22);
static_assert(SeedLimit(3, 342000, 1000000 / 60) == 23);
static_assert(SeedLimit(8, 8 * (1000000 / 60), 1000000 / 60) == 8);
static_assert(SeedLimit(8, 524000) == MaxSeedPairs);
static_assert(SeedLimit(3, std::numeric_limits<uint64_t>::max()) == MaxSeedPairs);

static void Test(unsigned seats, uint64_t handshakeUs, uint64_t firstCompleteUs)
{
  const unsigned limit = SeedLimit(seats, handshakeUs);
  unsigned sent = 0;
  for (uint64_t t = 0;; t += FrameUs)
  {
    const bool complete = t >= firstCompleteUs;
    if (!ShouldSeed(seats, limit, sent, false, complete))
    {
      assert(complete && sent >= seats && sent <= limit);
      assert(!ShouldSeed(seats, limit, sent, true, false));
      break;
    }
    ++sent;
    assert(sent <= MaxSeedPairs);
  }
}

int main()
{
  Test(3, 35000, 49000);
  // Repeated FF sends must not turn a 342 ms full round into one vblank.
  uint64_t ff = 0;
  for (uint64_t now = 1000000; now < 1342000; now += FrameUs) ff = FirstFfStamp(ff, now);
  assert(ff == 1000000);
  Test(3, 1342000 - ff, 350000); // slower than the old 3-frame seed + 250 ms wait
  Test(4, 245000, 255000);
  Test(8, 370000, 398000);
  const auto limit = SeedLimit(8, 780000);
  for (unsigned p = 0; p < limit; ++p) assert(ShouldSeed(8, limit, p, false, false));
  assert(!ShouldSeed(8, limit, limit, false, false)); // a lost ring remains bounded
  std::puts("PASS Indy multi-seat native startup: quick ring, delayed first return, stop on first complete pair, finite cap");
}
