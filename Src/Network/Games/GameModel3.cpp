// SPDX-License-Identifier: GPL-3.0-or-later
#include "Supermodel.h"
#include "Model3/Model3.h"
#include "Daytona2/Daytona2Relay.h"
#include "NativeSessionSettings.h"
#include "Policy.h"
#include "Scud/Policy.h"
#include "Srally2/Policy.h"

bool CModel3::HoldForGameNetwork()
{
  const bool relay = !m_config["NetRelay"].ValueAsDefault<std::string>("").empty();
  if (Srally2::IsSupportedGame(m_game.name) && relay && !m_gameNetwork.m_srally2RoleConfigured)
  {
    const int index = NetBoard->RelayCabinetIndex();
    if (index < 0 || m_gameNetwork.m_relayBootstrapFrames >= 1800)
      return true;

    auto image = EEPROM.GetImage();
    const bool eightPlayers = Srally2Research::Selected(m_config);
    if (Srally2::ConfigureRelayEeprom(image, unsigned(index), m_game.name, eightPlayers) &&
        NativeSessionSettings::Apply(image, m_game.name,
            m_config["NetNativeRoomOptions"].ValueAsDefault<unsigned>(0), 0))
    {
      EEPROM.SetImage(image);
      m_gameNetwork.m_srally2RoleConfigured = true;
      if (m_gameNetwork.m_relayBootstrap) Reset();
      NetBoard->SetRelayNativeReady();
      InfoLog("Relay: configured Sega Rally 2 cabinet %u as %s; NVRAM changes are session-only",
          unsigned(index + 1), index == 0 ? "MASTER" : "SLAVE");
    }
    else
    {
      m_gameNetwork.m_relayBootstrap = true;
      if (++m_gameNetwork.m_relayBootstrapFrames == 1800)
        ErrorLog("Relay: Sega Rally 2 EEPROM did not initialize; check the test screen and restart.");
    }
    return false;
  }

  if (!Srally2::IsSupportedGame(m_game.name) && GameNetwork::IsSupportedGame(m_game.name) &&
      relay && !m_gameNetwork.m_relayConfigured)
  {
    const int index = NetBoard->RelayCabinetIndex();
    if (index < 0 || m_gameNetwork.m_relayBootstrapFrames >= 1800)
      return true;

    auto image = EEPROM.GetImage();
    const bool configured = Dayto2pe::IsSupportedGame(m_game.name)
        ? Dayto2pe::ConfigureRelayEeprom(image, unsigned(index), m_game.name,
              m_config["NetSpectate"].ValueAsDefault<bool>(false))
        : Lemans24::IsSupportedGame(m_game.name)
            ? Lemans24::ConfigureRelayEeprom(image, unsigned(index), m_game.name)
            : Scud::ConfigureRelayEeprom(image, unsigned(index), m_game.name);

    if (configured && NativeSessionSettings::Apply(image, m_game.name,
          m_config["NetNativeRoomOptions"].ValueAsDefault<unsigned>(0),
          m_config["NetNativeCarNumber"].ValueAsDefault<unsigned>(0)))
    {
      EEPROM.SetImage(image);
      m_gameNetwork.m_relayConfigured = true;
      if (m_gameNetwork.m_relayBootstrap) Reset();
      NetBoard->SetRelayNativeReady();
      InfoLog("Relay: configured %s cabinet %u; NVRAM changes are session-only",
          index == 0 ? "MASTER" : "SLAVE", unsigned(index + 1));
    }
    else
    {
      m_gameNetwork.m_relayBootstrap = true;
      if (++m_gameNetwork.m_relayBootstrapFrames == 1800)
        ErrorLog("Relay: game EEPROM did not initialize; check the test screen and restart.");
    }
  }

  if (!m_config["SimulateNet"].ValueAs<bool>() ||
      !m_config["AsyncNet"].ValueAsDefault<bool>(false))
    return false;
  if (Dayto2pe::IsSupportedGame(m_game.name)) return HoldForDaytonaNetwork();
  if (Scud::IsSupportedGame(m_game.name)) return HoldForScudNetwork();
  if (Lemans24::IsSupportedGame(m_game.name)) return HoldForLemans24Network();
  return false;
}
