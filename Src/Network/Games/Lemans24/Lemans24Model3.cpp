// Le Mans 24 (lemans24) verified native pre-countdown start barrier.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Supermodel.h"
#include "Model3/Model3.h"
#include "Policy.h"

bool CModel3::HoldForLemans24Network()
{
  const uint32_t stage = Read32(0x17f61c);
  const uint32_t faultSub = Read32(0x1604f0);
  const uint32_t raceMode = Read32(0x17e7e0);
  const uint32_t stageSub = Read32(0x18d298);
  const uint32_t participants=NetBoard->NativeEntryParticipants();

  // Correct the native joined count when it includes a duplicated peer.
  if (((stage==1u && stageSub>=9u) || stage==2u) && participants) {
    unsigned joinedPeers=0;
    for(uint32_t bits=participants;bits;bits&=bits-1) ++joinedPeers;
    --joinedPeers;
    const uint32_t joined=Read32(0x17cc8c);
    if(joined>joinedPeers && joined<=2u*joinedPeers) Write32(0x17cc8c,joinedPeers);
  }
  // Keep Challenger Entry running normally; it is the native join window.
  const bool startGate = Lemans24::AtStartGate(stage, faultSub, raceMode, stageSub);
  const bool startLeft = stage == 3u && faultSub == 0u &&
                         Lemans24::IsLinkedRaceMode(raceMode) && stageSub == 3u;
  const bool gate=startGate, entry=startLeft;
  unsigned entrants=0;
  for(uint32_t bits=participants;bits;bits&=bits-1) ++entrants;
  const bool nativeCountMatches=entrants && Read32(0x17cc8c)==entrants-1;
  NetBoard->SetRaceParticipants(nativeCountMatches ? participants : 0);

  if (stage <= 16u && faultSub <= 16u && raceMode <= 16u && stageSub <= 32u)
  {
    static uint32_t lastTuple = ~0u;
    const uint32_t gateBits = (startGate ? 2u : 0u);
    const uint32_t tuple = ((stage & 0x3Fu) << 26) | ((gateBits & 0x3u) << 24) |
                           ((faultSub & 0xFFu) << 16) | ((raceMode & 0xFFu) << 8) |
                           (stageSub & 0xFFu);
    if (tuple != lastTuple)
    {
      lastTuple = tuple;
      InfoLog("%s stage: stage=%u fault=%u mode=%u sub=%u%s%s%s",
              m_game.name.c_str(), unsigned(stage), unsigned(faultSub),
              unsigned(raceMode), unsigned(stageSub),
              "",
              startGate ? " [start gate]" : "",
              entry ? " [left gate]" : "");
    }
  }
  return NetBoard->HoldForRaceStart(gate, entry);
}
