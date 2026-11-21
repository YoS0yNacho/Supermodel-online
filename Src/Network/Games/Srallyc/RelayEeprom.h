// Rev C Twin/DX service settings in the serial 93C46 (not the backup RAM).
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_SRALLYC_RELAY_EEPROM_H
#define INCLUDED_SRALLYC_RELAY_EEPROM_H

#include "Network/Online/RoomSettings.h"
#include <array>
#include <cstdint>

namespace SrallycOnline {
using EepromImage = std::array<uint16_t, 64>;
inline constexpr unsigned SettingsBytes = 0x24, RoleWord = 5, DifficultyModeWord = 6;
inline constexpr uint32_t ProgramCRC = 0xe7767463u;  // merged Rev C program, zero-padded to 2 MiB

// Stored as little-endian words. Complement of CRC-CCITT over bytes 2..35,
// initialized to 0xffff; verified against all seat/difficulty/mode values
// written by Revision C's own GAME ASSIGNMENTS menu.
constexpr uint16_t SettingsChecksum(const EepromImage& image)
{
  uint16_t crc = 0xffff;
  for (unsigned i = 1; i < SettingsBytes/2; ++i)
    for (unsigned shift : {0u, 8u})
    {
      crc ^= uint16_t(((image[i] >> shift) & 0xffu) << 8);
      for (unsigned bit = 0; bit < 8; ++bit)
        crc = uint16_t((unsigned(crc) << 1) ^ (crc & 0x8000 ? 0x1021 : 0));
    }
  return uint16_t(~crc);
}

inline bool ConfigureRelayEeprom(EepromImage& image, unsigned seat, uint32_t options)
{
  if (seat < 1 || seat > Cabinets || !NativeRoomSettings::Valid(Game, options) ||
      image[1] != SettingsBytes || image[0] != SettingsChecksum(image))
    return false;

  // Byte 11 = CAR 1..4, byte 12 = difficulty, byte 13 = game mode.
  // Keep every other setting (including cabinet type, country, coinage and
  // calibration). The lobby sorts Easy before Normal; the EEPROM does not.
  constexpr unsigned nativeDifficulty[] = {1, 0, 2, 3};
  image[RoleWord] = uint16_t((image[RoleWord] & 0xffu) | (seat << 8));
  image[DifficultyModeWord] = uint16_t(nativeDifficulty[NativeRoomSettings::Difficulty(options)] |
                                      (NativeRoomSettings::Mode(options) << 8));
  image[0] = SettingsChecksum(image);
  return true;
}
}
#endif
