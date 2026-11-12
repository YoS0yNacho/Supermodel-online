// Research helper only: NOT connected to emulator transport. SPDX-License-Identifier: GPL-3.0-or-later
// Research-only packet transform; it is not connected to production synchronization.
#ifndef INCLUDED_SRALLY2_START_ASSIST_RESEARCH_H
#define INCLUDED_SRALLY2_START_ASSIST_RESEARCH_H
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace Srally2 { namespace Research {
constexpr size_t SegmentSize = 2048;
constexpr size_t CabinetId = 0x00, Counter = 0x14, Phase = 0x18;
constexpr size_t Coordinator = 0x44, Acknowledgement = 0x6c;
// HLE wire words swap their 16-bit halves.
inline uint32_t Word(const uint8_t* p) {
  return (uint32_t(p[2]) << 24) | (uint32_t(p[3]) << 16) |
         (uint32_t(p[0]) << 8) | uint32_t(p[1]);
}
// Mutates a private outgoing copy only and rejects unexpected layouts.
inline bool MirrorParticipantHandshake(uint8_t* output, size_t outputSize,
                                       const uint8_t* leader, size_t leaderSize) {
  if (!output || !leader || outputSize != SegmentSize || leaderSize != SegmentSize ||
      Word(output + CabinetId) != 1 || Word(leader + CabinetId) != 0 ||
      Word(output + Phase) != 10 || Word(leader + Coordinator) > 3)
    return false;
  std::copy_n(leader + Counter, 4, output + Counter);
  std::copy_n(leader + Coordinator, 4, output + Acknowledgement);
  return true;
}
}}
#endif
