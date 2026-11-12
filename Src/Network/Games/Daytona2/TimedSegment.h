// Async dayto2pe timestamp sidecar. Native netboard bytes remain untouched.
// GPL-3.0-or-later.
#ifndef INCLUDED_DAYTONA2_TIMEDSEGMENT_H
#define INCLUDED_DAYTONA2_TIMEDSEGMENT_H
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace Dayto2pe
{
#pragma pack(push, 1)
struct TimedSegmentHeader
{
  uint32_t magic = 0x31544d44; // DMT1
  uint16_t version = 1;
  uint16_t headerSize = 48;
  uint32_t segmentSize = 0;
  uint32_t hops = 0;
  uint64_t sourceTimestampUs = 0; // source machine's steady clock
  uint64_t lastHopSendUs = 0;    // clock domain named by the last sender
  uint64_t ageUs = 0;            // accumulated one-way age estimate
  uint64_t uncertaintyUs = 0;    // accumulated uncertainty estimate
};
#pragma pack(pop)
static_assert(sizeof(TimedSegmentHeader) == 48, "timed segment header wire size");
constexpr uint64_t MaxTimingUncertaintyUs = 60000000;

inline bool IsTimedSegment(const std::vector<uint8_t>& wire, size_t expectedSegmentSize)
{
  if (wire.size() != sizeof(TimedSegmentHeader) + expectedSegmentSize) return false;
  TimedSegmentHeader h;
  std::memcpy(&h, wire.data(), sizeof(h));
  return h.magic == TimedSegmentHeader{}.magic && h.version == 1 &&
         h.headerSize == sizeof(h) && h.segmentSize == expectedSegmentSize && h.hops < 64 &&
         h.sourceTimestampUs <= uint64_t(std::numeric_limits<int64_t>::max()) &&
         h.lastHopSendUs <= uint64_t(std::numeric_limits<int64_t>::max()) &&
         h.ageUs <= 10000000 && h.uncertaintyUs <= MaxTimingUncertaintyUs;
}

inline std::vector<uint8_t> MakeTimedSegment(const uint8_t* raw, size_t size, uint64_t nowUs,
                                             uint64_t sourceUncertaintyUs = 0,
                                             uint64_t sourceTimestampUs = 0)
{
  TimedSegmentHeader h;
  h.segmentSize = uint32_t(size);
  h.sourceTimestampUs = sourceTimestampUs ? sourceTimestampUs : nowUs;
  h.lastHopSendUs = nowUs;
  h.uncertaintyUs = std::min(sourceUncertaintyUs, MaxTimingUncertaintyUs);
  std::vector<uint8_t> wire(sizeof(h) + size);
  std::memcpy(wire.data(), &h, sizeof(h));
  if (size) std::memcpy(wire.data() + sizeof(h), raw, size);
  return wire;
}

// Estimate transit + local receive-queue time by translating the previous
// sender's monotonic timestamp using its direct TCP clock sample. The existing
// ping/pong offset assumes near-symmetric paths; RTT/2 is retained as explicit
// uncertainty rather than claimed as a precise one-way measurement.
inline bool AdvanceTimedSegment(std::vector<uint8_t>& wire, size_t expectedSegmentSize,
                                uint64_t localNowUs, int64_t peerMinusLocalUs,
                                int64_t rttUs, uint64_t clockSampleAgeUs)
{
  if (!IsTimedSegment(wire, expectedSegmentSize)) return false;
  TimedSegmentHeader h;
  std::memcpy(&h, wire.data(), sizeof(h));
  if (rttUs < 0 || rttUs > 2000000 || clockSampleAgeUs > 3000000 ||
      h.lastHopSendUs > uint64_t(std::numeric_limits<int64_t>::max()))
  {
    h.uncertaintyUs = MaxTimingUncertaintyUs;
  }
  else
  {
    const int64_t peerTime = int64_t(h.lastHopSendUs);
    if (peerMinusLocalUs < 0 && peerTime > std::numeric_limits<int64_t>::max() + peerMinusLocalUs)
      h.uncertaintyUs = MaxTimingUncertaintyUs;
    else
    {
    const int64_t localSend = peerTime - peerMinusLocalUs;
    constexpr uint64_t ClockJitterSlackUs = 15000; // tolerate <=15 ms residual asymmetric clock skew
    if (localSend < 0 ||
        (uint64_t(localSend) > localNowUs && uint64_t(localSend) - localNowUs > ClockJitterSlackUs) ||
        (localNowUs >= uint64_t(localSend) && localNowUs - uint64_t(localSend) > 10000000))
      h.uncertaintyUs = MaxTimingUncertaintyUs;
    else
    {
      const uint64_t elapsed = localNowUs >= uint64_t(localSend) ? (localNowUs - uint64_t(localSend)) : 0;
      const uint64_t skewPenalty = uint64_t(localSend) > localNowUs ? (uint64_t(localSend) - localNowUs) : 0;
      h.ageUs = std::min<uint64_t>(10000000, h.ageUs + elapsed);
      // Conservative engineering bound: half the measured RTT for midpoint
      // offset error, 1 ms timestamp/scheduling allowance, and 100 ppm drift.
      const uint64_t uncertainty = uint64_t(rttUs) / 2 + 1000 + clockSampleAgeUs / 10000 + skewPenalty;
      h.uncertaintyUs = std::min(MaxTimingUncertaintyUs, h.uncertaintyUs + uncertainty);
    }
    }
  }
  h.lastHopSendUs = localNowUs;
  ++h.hops;
  std::memcpy(wire.data(), &h, sizeof(h));
  return true;
}

inline bool ReadTimedSegmentHops(const std::vector<uint8_t>& wire, size_t expectedSegmentSize,
                                 uint32_t& hops)
{
  if (!IsTimedSegment(wire, expectedSegmentSize)) return false;
  TimedSegmentHeader h;
  std::memcpy(&h, wire.data(), sizeof(h));
  hops = h.hops;
  return true;
}

inline bool ReadTimedSegment(const std::vector<uint8_t>& wire, size_t expectedSegmentSize,
                             const uint8_t*& raw, uint64_t& ageUs, uint64_t& uncertaintyUs)
{
  if (!IsTimedSegment(wire, expectedSegmentSize)) return false;
  TimedSegmentHeader h;
  std::memcpy(&h, wire.data(), sizeof(h));
  raw = wire.data() + sizeof(h);
  ageUs = h.ageUs;
  uncertaintyUs = h.uncertaintyUs;
  return true;
}

inline bool ReadTimedSegmentSourceUs(const std::vector<uint8_t>& wire, size_t expectedSegmentSize,
                                     uint64_t& sourceTimestampUs)
{
  if (!IsTimedSegment(wire, expectedSegmentSize)) return false;
  TimedSegmentHeader h;
  std::memcpy(&h, wire.data(), sizeof(h));
  sourceTimestampUs = h.sourceTimestampUs;
  return true;
}
}
#endif
