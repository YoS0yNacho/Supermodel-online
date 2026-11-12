/**
 ** Supermodel
 ** A Sega Model 3 Arcade Emulator.
 ** Copyright 2003-2026 The Supermodel Team
 **
 ** This file is part of Supermodel.
 **
 ** Supermodel is free software: you can redistribute it and/or modify it under
 ** the terms of the GNU General Public License as published by the Free
 ** Software Foundation, either version 3 of the License, or (at your option)
 ** any later version.
 **
 ** Supermodel is distributed in the hope that it will be useful, but WITHOUT
 ** ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 ** FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 ** more details.
 **
 ** You should have received a copy of the GNU General Public License along
 ** with Supermodel.  If not, see <http://www.gnu.org/licenses/>.
 **/

// Called between completed frames for Daytona role setup and start gating.
#include "Supermodel.h"
#include "Model3/Model3.h"
#include "Daytona2Relay.h"
#include "Daytona2StartSync.h"
#include "Daytona2CpuPrediction.h"
#include "Policy.h"
#include "Network/Games/NativeRaceMembership.h"

bool CModel3::HoldForDaytonaNetwork()
{
  // Snapshot CPU ownership; prediction never writes native flags or IDs.
  if (Dayto2pe::IsSupportedGame(m_game.name) && m_config["SimulateNet"].ValueAs<bool>() &&
      m_config["AsyncNet"].ValueAsDefault<bool>(false))
  {
    const auto& profile = *Dayto2pe::GetProfile(m_game.name);
    Dayto2pe::CpuRouting routing;
    routing.local = Read8(0x10062b);
    routing.machines = Read8(0x10062a);
    for (unsigned i = 0; i < routing.owners.size(); ++i)
    {
      const unsigned base = profile.owners + i * 0x2c;
      auto& owner = routing.owners[i];
      owner.format = Read8(base);
      owner.count = Read8(base + 3);
      for (unsigned j = 0; j < owner.ids.size(); ++j) owner.ids[j] = Read8(base + 4 + j);
    }
    for (unsigned i = 0; i < routing.types.size(); ++i)
    {
      routing.types[i] = Read8(profile.cars + 4 * i);
      routing.authorities[i] = Read8(profile.cars + 2 + 4 * i);
    }
    NetBoard->SetDaytona2CpuRouting(routing);
    NetBoard->SetRaceParticipants(routing.machines==NetBoard->SyncCabinetCount() &&
      int(routing.local)==NetBoard->SyncCabinetIndex()+1 ? GameNetwork::DaytonaRaceParticipants(routing) : 0);
  }

  // Gate after scene setup and before intro, never at the GO checkpoint.
  if (Dayto2pe::IsSupportedGame(m_game.name) && m_config["SimulateNet"].ValueAs<bool>() &&
      m_config["AsyncNet"].ValueAsDefault<bool>(false))
  {
    const bool atGate = Dayto2pe::AtStartGate(Read8(0x105004), Read8(0x105005),
      Read8(0x105006), Read32(0x105008));
    const bool nativeEntry = Read8(0x105004) == 15 && Read8(0x105005) == 15 &&
      ((Read8(0x105006) == 5 && Read32(0x105008) == 1) || Read8(0x105006) == 6);
    static unsigned last = ~0u;
    const unsigned current = Read8(0x105004), next = Read8(0x105005), sub = Read8(0x105006);
    const unsigned tuple = (current << 16) | (next << 8) | sub;
    if (tuple != last)
    {
      last = tuple;
      InfoLog("%s stage: current=%u next=%u substate=%u%s%s", m_game.name.c_str(), current, next, sub,
        atGate ? " [start gate]" : "", nativeEntry ? " [left gate]" : "");
    }
    if (NetBoard->HoldForRaceStart(atGate, nativeEntry))
    {
      return true;
    }
  }

  return false;
}
