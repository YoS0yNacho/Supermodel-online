// Mandatory runtime-only Online relay policy. GPL-3.0-or-later.
#ifndef INCLUDED_ONLINE_SESSIONPOLICY_H
#define INCLUDED_ONLINE_SESSIONPOLICY_H
#include "Util/NewConfig.h"
namespace Online {
inline constexpr unsigned FrameMilliHz(bool relay) { return relay ? 57524u : 60000u; }
inline constexpr double FrameDurationUs(bool relay) { return 1000000000.0 / FrameMilliHz(relay); }
inline void EnforceRelayTiming(Util::Config::Node& config) {
  if (config["NetRelay"].ValueAsDefault<std::string>("").empty()) return;
  config.Set("RefreshRate", 57.524f);
  config.Set("Throttle", true);
  config.Set("VSync", false);
}
// Integer milliHz accumulator preserves the negotiated real-time send rate
// at either emulation refresh rate; cap accumulated debt after a link stall.
inline unsigned AccumulateTick(unsigned phase, unsigned hz, unsigned frameMilliHz) {
  return phase < 2u * frameMilliHz ? phase + 1000u * hz : phase;
}
inline unsigned ConsumeTick(unsigned phase, unsigned frameMilliHz) {
  return phase - frameMilliHz < frameMilliHz ? phase - frameMilliHz : frameMilliHz - 1u;
}
}
#endif
