// Adaptive update tick rate: the room host (creator PlayerHost, or relay.py
// which mirrors this file) lowers the negotiated tick rate while the link or
// the host is congested and raises it again once the link has been clean for
// a while. The room's configured rate is the ceiling; it is never exceeded.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_ONLINE_ADAPTIVE_TICK_RATE_H
#define INCLUDED_ONLINE_ADAPTIVE_TICK_RATE_H
#include <algorithm>
#include <cstdint>
#include <vector>
namespace Online {
// Capability bit 8 in the lobby HELLO: the client accepts RLT2 (adaptive room
// announcement) and mid-session RLT1 rate changes, and reports its receive-side
// queueing delay / loss in the UDP hello (Header::total ms, Header::part %).
constexpr uint32_t AdaptiveTickCapability = 8;
// Thresholds shared by every host: a second whose p50 one-way queueing delay
// exceeds this, or whose loss does, is congested.
constexpr int64_t QueueDelayThresholdUs = 40000;
constexpr unsigned LossThresholdPercent = 5;

// One-way delay meter without synchronized clocks: `delay` is (arrival time on
// the receiver clock - send stamp on the sender clock). Its running minimum is
// the uncongested path; the excess over it is queueing (the LEDBAT idea). The
// baseline inflates 0.25 ms per second so clock drift and route changes cannot
// pin it below reality forever.
struct DelayMeter {
  static constexpr int64_t Unset = INT64_MAX;
  int64_t baseline = Unset;
  std::vector<int64_t> window;
  void Note(int64_t delayUs) {
    if (baseline == Unset || delayUs < baseline) baseline = delayUs;
    if (window.size() < 4096) window.push_back(delayUs - baseline);
  }
  // Closes the one-second window: p50 queueing delay in us (0 when empty).
  int64_t Close() {
    int64_t p50 = 0;
    if (!window.empty()) {
      auto mid = window.begin() + std::ptrdiff_t(window.size() / 2);
      std::nth_element(window.begin(), mid, window.end());
      p50 = *mid;
    }
    window.clear();
    if (baseline != Unset) baseline += 250;
    return p50;
  }
};
// Loss from per-origin sequence numbers (one per snapshot, parts share it).
struct LossMeter {
  bool seen = false;
  uint32_t last = 0;
  uint64_t expected = 0, received = 0;
  void Note(uint32_t sequence) {
    if (!seen) { seen = true; last = sequence; expected = received = 1; return; }
    const uint32_t gap = sequence - last;
    if (gap == 0 || gap > 0x80000000u) { ++received; return; }  // duplicate or late arrival
    expected += gap; ++received; last = sequence;
  }
  // Closes the window: loss percent, 0 when fewer than 10 snapshots were expected.
  unsigned ClosePercent() {
    unsigned percent = 0;
    if (expected >= 10 && received < expected) percent = unsigned((expected - received) * 100 / expected);
    expected = received = 0;
    return percent;
  }
};
// Rate ladder controller, stepped once per second.
struct AdaptiveTickRate {
  static constexpr unsigned Ladder[] = {60, 45, 30, 24, 20, 15, 12, 10};
  static constexpr unsigned CongestedSeconds = 2;   // consecutive congested seconds before a step down
  static constexpr unsigned CleanSeconds = 20;      // consecutive clean seconds before a step up
  static constexpr uint64_t DownDwellUs = 3000000;  // between two steps down
  static constexpr uint64_t UpDwellUs = 20000000;   // after any change before a step up
  unsigned cap = 60, floor = 10, current = 60, congested = 0, clean = 0;
  uint64_t lastChangeUs = 0;
  void Reset(unsigned capHz) {
    cap = current = std::clamp(capHz, 5u, 60u);
    floor = std::min(10u, cap);
    congested = clean = 0; lastChangeUs = 0;
  }
  static unsigned Lower(unsigned rate, unsigned floor) {
    unsigned best = floor;
    for (unsigned step : Ladder) if (step < rate && step >= floor && step > best) best = step;
    return std::min(best, rate);
  }
  static unsigned Higher(unsigned rate, unsigned cap) {
    unsigned best = cap;
    for (unsigned step : Ladder) if (step > rate && step <= cap && step < best) best = step;
    return std::max(best, rate);
  }
  // True when `current` changed; the caller then announces it to every cabinet.
  bool Update(bool congestedNow, uint64_t nowUs) {
    if (congestedNow) {
      clean = 0;
      if (++congested >= CongestedSeconds && current > floor && (!lastChangeUs || nowUs - lastChangeUs >= DownDwellUs)) {
        current = Lower(current, floor); lastChangeUs = nowUs; congested = 0; return true;
      }
      return false;
    }
    congested = 0;
    if (++clean >= CleanSeconds && current < cap && (!lastChangeUs || nowUs - lastChangeUs >= UpDwellUs)) {
      current = Higher(current, cap); lastChangeUs = nowUs; clean = 0; return true;
    }
    return false;
  }
};
}
#endif
