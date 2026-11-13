// Read-only native race rosters. Connection/credit/input state is NOT membership.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_NATIVE_RACE_MEMBERSHIP_H
#define INCLUDED_NATIVE_RACE_MEMBERSHIP_H
#include "RaceStart.h"
#include <array>
namespace GameNetwork {
inline uint32_t ScudRaceParticipants(unsigned machines,unsigned local,unsigned humans,
                                     const std::array<uint8_t,8>& players) {
  if(!machines || machines>8 || !humans || humans>8 || !local || local>machines) return 0;
  uint32_t mask=0;
  for(unsigned i=0;i<humans;++i) {
    if(!players[i] || players[i]>machines) return 0;
    mask|=1u<<(players[i]-1); // twin human cars may share a physical cabinet
  }
  return RaceStart::ValidRoster(mask,machines,local-1) ? mask : 0;
}
template<class Routing> uint32_t DaytonaRaceParticipants(const Routing& route) {
  // Daytona USA 2 / Power Edition support up to 16 linked cabinets (Dayto2pe::MaxCabinets).
  // The previous 8-cabinet cap was a copy-paste from Scud Race and broke 9-16 player rooms
  // at the start gate ("cannot validate native race participants; no guessed roster").
  if(!route.machines || route.machines>16 || !route.local || route.local>route.machines) return 0;
  uint32_t mask=0;
  for(unsigned car=0;car<route.types.size();++car) if(route.types[car]==1) {
    const unsigned cabinet=route.authorities[car];
    if(!cabinet || cabinet>route.machines || (mask&(1u<<(cabinet-1)))) return 0;
    mask|=1u<<(cabinet-1);
  }
  return RaceStart::ValidRoster(mask,route.machines,route.local-1) ? mask : 0;
}
}
#endif
