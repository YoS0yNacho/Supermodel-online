// Experimental dayto2pe in-game-entry barrier protocol. GPL-3.0-or-later.
#ifndef INCLUDED_DAYTONA2STARTSYNC_H
#define INCLUDED_DAYTONA2STARTSYNC_H
#include <cstdint>
#include "../RaceStart.h"
namespace Dayto2pe
{
inline bool AtStartGate(unsigned mainState, unsigned nextState, unsigned subState, uint32_t timer)
{
  // Hold after scene setup, before the rolling intro; never gate the GO checkpoint.
  return mainState == 15 && nextState == 15 && subState == 5 && timer == 2;
}
using RaceStart::StartToken;
using RaceStart::TranslateStartDeadline;
}
#endif
