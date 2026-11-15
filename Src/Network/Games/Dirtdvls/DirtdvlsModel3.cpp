// Dirt Devils (dirtdvls) verified native pre-countdown start barrier.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Supermodel.h"
#include "Model3/Model3.h"
#include "Policy.h"
#include "Prediction.h"

bool CModel3::HoldForDirtdvlsNetwork()
{
  using namespace Dirtdvls;
  const uint32_t state = Read32(Ram::GameState);
  const uint32_t raceSub = Read32(Ram::RaceSubstate);
  const uint32_t machines = Read32(Ram::LinkMachines);
  const uint32_t local = Read32(Ram::LinkLocalIndex);
  const uint32_t players = Read32(Ram::LinkPlayers);

  // Native roster: every linked cabinet whose last packet is in the race scene,
  // cross-checked against the ROM's own linked player count.
  uint32_t participants = 0;
  if (machines >= 2 && machines <= MaxCabinets && local < machines &&
      machines == unsigned(NetBoard->SyncCabinetCount()) &&
      int(local) == NetBoard->SyncCabinetIndex() && InRaceScene(state)) {
    participants = 1u << local;
    for (uint32_t i = 0; i < machines; ++i) {
      if (i == local) continue;
      const uint32_t remote = Read32(Ram::RxBuffers + i * Ram::RxStride + StateOffset);
      if (InRaceScene(remote)) participants |= 1u << i;
    }
    unsigned count = 0;
    for (uint32_t bits = participants; bits; bits &= bits - 1) ++count;
    if (count != players) participants = 0;
  }
  NetBoard->SetRaceParticipants(participants);

  const bool gate = AtStartGate(state, raceSub);
  const bool entry = LeftStartGate(state, raceSub);
  static uint32_t lastTuple = ~0u;
  const uint32_t tuple = (state & 0x7FFF00FFu) | ((raceSub & 0xFFu) << 8) |
                         (gate ? 0x80000000u : 0u);
  if (tuple != lastTuple)
  {
    lastTuple = tuple;
    const StateWord s = DecodeState(state);
    InfoLog("%s stage: mode=%u scene=%u step=%u sub=%u players=%u roster=%02x%s%s",
            m_game.name.c_str(), unsigned(s.mode), unsigned(s.gameScene), unsigned(s.step),
            unsigned(raceSub), unsigned(players), unsigned(participants),
            gate ? " [start gate]" : "", entry ? " [left gate]" : "");
  }
  return NetBoard->HoldForRaceStart(gate, entry);
}
