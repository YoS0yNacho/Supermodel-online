// Independently verified exact-ROM profiles. GPL-3.0-or-later.
#ifndef INCLUDED_DAYTONA2_NETWORK_POLICY_H
#define INCLUDED_DAYTONA2_NETWORK_POLICY_H
#include <cstdint>
#include <string>
namespace Dayto2pe {
// Keep the Power Edition wire identity and behavior unchanged. Battle on the
// Edge has a different CPU body and must never share a room or direct ring.
constexpr unsigned MaxCabinets = 16;
constexpr uint64_t AsyncGUID = 0x5bf177da34875ULL;
struct Profile {
  uint64_t guid;
  uint32_t owners, cars;
  unsigned compactHeader;
  uint16_t eepromRevision;
  unsigned stateShift, raceState, classOffset, classMask, classValue;
};
inline constexpr Profile PowerEdition{AsyncGUID, 0x72c404, 0x72cad4, 0xd0, 0x0144, 27, 16, 8, 7, 2};
inline constexpr Profile BattleOnTheEdge{0x5bf177da34878ULL, 0x7335cc, 0x733c9c, 0xd4, 0x0143, 16, 13, 4, 0x1c00, 0x800};
inline const Profile* GetProfile(const std::string& name) {
  return name == "dayto2pe" ? &PowerEdition : name == "daytona2" ? &BattleOnTheEdge : nullptr;
}
inline bool IsSupportedGame(const std::string& name) { return GetProfile(name) != nullptr; }
inline bool AsyncEnabled(const std::string& name, bool async) { return async && IsSupportedGame(name); }
inline uint64_t ProtocolGUID(const std::string& name) { const auto* p=GetProfile(name); return p?p->guid:0; }
}
#endif
