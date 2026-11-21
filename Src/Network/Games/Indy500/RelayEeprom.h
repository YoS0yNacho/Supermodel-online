// INDY 500 Twin Rev A Online cabinet assignment. SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_INDY500_RELAY_EEPROM_H
#define INCLUDED_INDY500_RELAY_EEPROM_H

#include "Network/Online/RoomSettings.h"
#include <array>
#include <cstdint>

namespace Indy500Online {
using EepromImage = std::array<uint16_t,64>;
inline constexpr unsigned PrimarySettings = 4, BackupSettings = 22;
inline constexpr unsigned SettingsWords = 17, ModeDifficultyWord = 7, HandicapWord = 8, RoleAndIdWord = 10;

// Rev A i960 routine 0x2c3c0: low byte first, 34 bytes after the checksum.
// Its polynomial step is addition (not the usual CRC-CCITT XOR).
constexpr uint16_t SettingsChecksum(const EepromImage& image, unsigned record)
{
  uint32_t sum = 0xdebdec00u;
  for (unsigned i = record + 1; i <= record + SettingsWords; ++i)
  {
    for (unsigned shift = 0; shift < 16; shift += 8)
    {
      sum = (sum & 0xffffff00u) | ((image[i] >> shift) & 0xffu);
      for (unsigned bit = 0; bit < 8; ++bit)
        sum = (sum << 1) + ((sum & 0x80000000u) ? 0x10210000u : 0u);
    }
  }
  return uint16_t(sum >> 16);
}

// Edit only a recognized initialized image. Follow the ROM's preference for
// the primary record; recover from the verified copy if the primary is bad.
// Apply the frozen lobby rules at the same checkpoint as cabinet assignment.
// Neither record is written to disk for a relay session.
inline bool ConfigureRelayEeprom(EepromImage& image, unsigned index, uint32_t options=0)
{
  if (index >= Cabinets || !NativeRoomSettings::Valid(Game,options) ||
      image[0] != 0x3353 || image[1] != 0x4132 || image[2] != 0x0907)
    return false;
  const unsigned source = image[PrimarySettings] == SettingsChecksum(image,PrimarySettings) ? PrimarySettings : BackupSettings;
  if (source == BackupSettings && image[source] != SettingsChecksum(image,source))
    return false;

  // Word bytes are LINK MODE (1 MASTER, 2 SLAVE) and zero-based CABINET ID.
  image[source + 1 + RoleAndIdWord] = uint16_t((index << 8) | (index == 0 ? 1u : 2u));
  if (options & NativeRoomSettings::Enabled)
  {
    // Rev A guest configuration bytes 0x53c758/59/5a (Game Assignments).
    // Mode and handicap are one bit each; difficulty uses the low 3 bits.
    auto& modeDifficulty = image[source + 1 + ModeDifficultyWord];
    modeDifficulty = uint16_t((modeDifficulty & ~0x0701u) | NativeRoomSettings::Mode(options) |
                              (NativeRoomSettings::Difficulty(options) << 8));
    auto& handicap = image[source + 1 + HandicapWord];
    handicap = uint16_t((handicap & ~1u) | unsigned(NativeRoomSettings::HasIndyHandicap(options)));
  }
  const unsigned other = source == PrimarySettings ? BackupSettings : PrimarySettings;
  for (unsigned i = 1; i <= SettingsWords; ++i)
    image[other + i] = image[source + i];
  image[PrimarySettings] = image[BackupSettings] = SettingsChecksum(image,source);
  return true;
}
}

#endif
