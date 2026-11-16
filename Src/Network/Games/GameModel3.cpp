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
            : Dirtdvls::IsSupportedGame(m_game.name)
                ? Dirtdvls::ConfigureRelayEeprom(image, unsigned(index), m_game.name)
                : Scud::ConfigureRelayEeprom(image, unsigned(index), m_game.name,
                      ScudResearch::Selected(m_config));

    const unsigned roomOptions = m_config["NetNativeRoomOptions"].ValueAsDefault<unsigned>(0);
    const unsigned carNumber = m_config["NetNativeCarNumber"].ValueAsDefault<unsigned>(0);
    const bool applied = configured &&
        NativeSessionSettings::Apply(image, m_game.name, roomOptions, carNumber,
            ScudResearch::Selected(m_config));
    if (applied)
    {
      EEPROM.SetImage(image);
      m_gameNetwork.m_relayConfigured = true;
      if (m_gameNetwork.m_relayBootstrap) Reset();
      NetBoard->SetRelayNativeReady();
      InfoLog("Relay: configured %s cabinet %u; NVRAM changes are session-only",
          index == 0 ? "MASTER" : "SLAVE", unsigned(index + 1));
    }
    else if (configured)
    {
      // The EEPROM image is fine; the room settings were rejected. Waiting
      // would not change anything, so report it now and boot with the native
      // defaults (every cabinet SLAVE) instead of a silent 30 s black screen.
      m_gameNetwork.m_relayBootstrapFrames = 1800;
      ErrorLog("Relay: room options %u / car %u are not valid for %s; native roles were NOT assigned. "
               "Update every client and the relay, then recreate the room.",
               roomOptions, carNumber, m_game.name.c_str());
    }
    else
    {
      m_gameNetwork.m_relayBootstrap = true;
      if (++m_gameNetwork.m_relayBootstrapFrames == 1800)
        ErrorLog("Relay: %s EEPROM did not initialize (image not recognized after 30 s); "
                 "native roles were NOT assigned. Delete NVRAM/%s.nv or check the test screen and restart.",
                 m_game.name.c_str(), m_game.name.c_str());
    }
  }

  if (!m_config["SimulateNet"].ValueAs<bool>() ||
      !m_config["AsyncNet"].ValueAsDefault<bool>(false))
    return false;
  if (Dayto2pe::IsSupportedGame(m_game.name)) return HoldForDaytonaNetwork();
  if (Scud::IsSupportedGame(m_game.name)) return HoldForScudNetwork();
  if (Lemans24::IsSupportedGame(m_game.name)) return HoldForLemans24Network();
  if (Dirtdvls::IsSupportedGame(m_game.name)) return HoldForDirtdvlsNetwork();
  return false;
}
