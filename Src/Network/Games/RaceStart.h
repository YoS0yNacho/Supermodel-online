#ifndef INCLUDED_NETWORK_RACE_START_H
#define INCLUDED_NETWORK_RACE_START_H
#include <cstdint>
namespace RaceStart {
inline unsigned Leader(uint32_t mask) {
  if (!mask) return 32;
  unsigned i=0; while (!(mask & (1u<<i))) ++i; return i;
}
inline bool ValidRoster(uint32_t mask, unsigned machines, unsigned local) {
  return machines>=1 && machines<=16 && local<machines && mask &&
    !(mask & ~((1u<<machines)-1)) && (mask & (1u<<local));
}
struct Transit {
  uint32_t epoch=0, serial=0, participants=0;
  int64_t offset=0, deadline=0;
  bool prepared=false, committed=false;
};
// Separate HLE control token, NEVER placed in guest communication RAM.
// Same little-endian host requirement as NetProtocol; all fields are fixed width.
struct StartToken
{
  uint32_t magic = 0x31533244; // D2S1
  uint32_t kind = 0;          // 1: collect readiness, 2: prepare, 3: commit
  uint32_t epoch = 0, serial = 0, hops = 0;
  uint32_t ready = 0, clocks = 0, reserved = 0;
  uint32_t origin = 0, participants = 0; // native race roster, NOT transport membership
  int64_t deadline = 0;       // sender's monotonic microseconds; translated per hop
  uint64_t maxRTT = 0;
};
static_assert(sizeof(StartToken) == 56, "start token wire size");
// Release lead chosen by the leader once every participant is ready. The
// prepare and commit tokens each need one ring round (tokens are forwarded by
// the emulation thread, so a round includes one frame of scheduling per hop),
// and the last cabinet must still see the deadline >= 100 ms ahead when the
// commit arrives. Three measured rounds plus a fixed margin covers that with
// 50% headroom; each declined prepare adds two more rounds.
constexpr uint64_t MinLeadUs = 500000, MaxLeadUs = 20000000, LeadMarginUs = 300000;
constexpr uint64_t CommitMarginUs = 150000;
constexpr unsigned MaxLeadRetries = 3;
inline uint64_t ReleaseLead(uint64_t roundUs, unsigned retries) {
  const uint64_t rounds = 3 + 2ull * retries;
  if (roundUs > MaxLeadUs / rounds) return MaxLeadUs + 1;
  const uint64_t lead = rounds * roundUs + LeadMarginUs;
  return lead < MinLeadUs ? MinLeadUs : lead;
}
// Cabinets a token has already visited on its way from `origin` to the
// cabinet `hops` hops downstream (the origin included, that cabinet excluded).
inline uint32_t UpstreamMask(unsigned origin, unsigned hops, unsigned machines) {
  uint32_t mask = 0;
  for (unsigned k = 0; k <= hops && k < machines; ++k) mask |= 1u << ((origin + k) % machines);
  return mask;
}
// Validate before converting to integer: malformed data must not overflow when
// a peer's monotonic clock has a very different epoch from ours.
inline bool TranslateStartDeadline(int64_t remote, int64_t peerMinusLocal,
                                   uint64_t now, int64_t& local)
{
  const long double translated = static_cast<long double>(remote) - peerMinusLocal;
  const long double ahead = translated - now;
  if (remote < 0 || ahead < 100000 || ahead > 20000000 || translated > INT64_MAX)
    return false;
  local = static_cast<int64_t>(translated);
  return true;
}
}
#endif
