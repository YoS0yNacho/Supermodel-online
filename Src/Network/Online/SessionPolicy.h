// Mandatory runtime-only Online relay policy. GPL-3.0-or-later.
#ifndef INCLUDED_ONLINE_SESSIONPOLICY_H
#define INCLUDED_ONLINE_SESSIONPOLICY_H
#include "Util/NewConfig.h"
#include "RoomSettings.h"
namespace Online {
// Emulated video rate of a relay session: 57.524 Hz (original hardware) unless
// the master selected the room's Video60 option, in which case every cabinet
// runs 60 Hz. Offline keeps the user's own RefreshRate.
inline constexpr unsigned FrameMilliHz(bool relay, bool video60=false) { return relay && !video60 ? 57524u : 60000u; }
inline constexpr double FrameDurationUs(bool relay, bool video60=false) { return 1000000000.0 / FrameMilliHz(relay, video60); }
inline bool RelayVideo60(const Util::Config::Node& config) {
  return NativeRoomSettings::HasVideo60(config["NetNativeRoomOptions"].ValueAsDefault<unsigned>(0));
}
// Software frame limiting at the room's emulated rate is mandatory online so a
// monitor cannot dictate cabinet speed. VSync is left exactly as configured:
// with it on, the throttle still paces emulation at 57.524/60 Hz and the swap
// merely waits for the next display refresh.
inline void EnforceRelayTiming(Util::Config::Node& config) {
  if (config["NetRelay"].ValueAsDefault<std::string>("").empty()) return;
  config.Set("RefreshRate", RelayVideo60(config) ? 60.0f : 57.524f);
  config.Set("Throttle", true);
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
