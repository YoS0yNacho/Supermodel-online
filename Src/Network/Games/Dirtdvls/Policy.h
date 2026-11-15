// Exact-ROM Dirt Devils (dirtdvls, Export Revision A) async policy.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_DIRTDVLS_NETWORK_POLICY_H
#define INCLUDED_DIRTDVLS_NETWORK_POLICY_H

#include <array>
#include <cstdint>
#include <string>

namespace Dirtdvls {

// Net board setup at CROM 0x0541E4: role word (0 MASTER, 1 SLAVE, 0xFFFF
// otherwise) from RAM 0x1227 -> net RAM 0x400, segment size halfword RAM 0x1228
// -> net RAM 0x408 (SimNetBoard type 2 RAM16[0x200]/[0x204]).
constexpr unsigned PacketSize = 0x0E00;  // 3584-byte segment (EEPROM word 13)
constexpr unsigned MaxCabinets = 4;      // service MACHINE ID 1..4
constexpr uint16_t EepromRevision = 0x0299;

inline bool IsSupportedGame(const std::string& game) {
  return game == "dirtdvls";
}

inline uint64_t ProtocolGUID(const std::string& game) {
  return game == "dirtdvls" ? 0x5bf177da3487cULL : 0ULL;
}

// Native service COMMUNICATION MODE (RAM 0x1227, EEPROM word 12 low byte):
// 0 = MASTER, 1 = SLAVE (factory), 2 = SATELLITE, 3 = NO LINK.
inline constexpr uint8_t CommModeForCabinet(unsigned index) {
  return index == 0 ? 0u : 1u;
}

// Main RAM addresses used by the start barrier and the standings overlay.
namespace Ram {
constexpr uint32_t GameState = 0x21AFA8;     // mode/step/attract scene/game scene
constexpr uint32_t RaceSubstate = 0x228EA0;  // 0 on the grid, nonzero from the countdown
constexpr uint32_t LinkStruct = 0x2257A8;    // +4 machines, +8 local index, +0x20 linked players
constexpr uint32_t LinkMachines = LinkStruct + 0x04;
constexpr uint32_t LinkLocalIndex = LinkStruct + 0x08;
constexpr uint32_t LinkPlayers = LinkStruct + 0x20;
constexpr uint32_t RxBuffers = 0x2257E0;     // one 0xDA0-byte receive copy per machine index
constexpr uint32_t RxStride = 0xDA0;
constexpr uint32_t CarCount = 0x22426C;
constexpr uint32_t LocalCar = 0x224270;
constexpr uint32_t RankTable = 0x223078;     // one-based 16-bit rank per car index
constexpr unsigned MaxCars = 8;
}

// Gate on the grid of the race scene before the native countdown starts; the
// grid actors are installed and every linked cabinet idles there for ~6 s.
inline constexpr bool AtStartGate(uint32_t state, uint32_t raceSubstate) {
  return (state >> 24) == 1u && (state & 0xFFu) == 2u && ((state >> 16) & 0xFFu) == 4u &&
         raceSubstate == 0u;
}
inline constexpr bool LeftStartGate(uint32_t state, uint32_t raceSubstate) {
  return (state >> 24) == 1u && (state & 0xFFu) == 2u && ((state >> 16) & 0xFFu) == 4u &&
         raceSubstate != 0u;
}

using EepromImage = std::array<uint16_t, 64>;

namespace Detail {
inline constexpr uint16_t CcittByte(uint8_t b) {
  uint16_t c = uint16_t(b) << 8;
  for (unsigned bit = 0; bit < 8; ++bit)
    c = (c & 0x8000u) ? uint16_t((c << 1) ^ 0x1021u) : uint16_t(c << 1);
  return c;
}
} // namespace Detail

// Same library checksum as Le Mans 24: words 6-34, seed 0xDEBDEB00.
inline uint16_t SettingsCRC(const EepromImage& image) {
  uint32_t acc = 0xDEBDEB00u;
  for (unsigned i = 6; i <= 34; ++i) {
    for (unsigned shift : {8u, 0u}) {
      const uint8_t b = uint8_t((image[i] >> shift) & 0xFFu);
      const uint16_t c = Detail::CcittByte(uint8_t(acc >> 24));
      acc = (((acc ^ (uint32_t(c) << 8)) | uint32_t(b)) << 8);
    }
  }
  const uint16_t c = Detail::CcittByte(uint8_t(acc >> 24));
  acc ^= (uint32_t(c) << 8);
  return uint16_t((acc >> 8) & 0xFFFFu);
}

// Service GAME ASSIGNMENTS image (RAM 0x120a + 2*word): word 12 = country
// (high byte, RAM 0x1226) | COMMUNICATION MODE (low byte, RAM 0x1227), word 13 =
// segment size (RAM 0x1228, copied to the net board), word 23 high byte =
// MACHINE ID - 1. Words 6 and 35 are save generation counters. Verified on the
// real ROM by changing both settings in service mode and reading the 93C46
// image back; the ROM only commits them on TEST MENU -> EXIT.
inline bool ConfigureRelayEeprom(EepromImage& image, unsigned index, const std::string& game) {
  if (!IsSupportedGame(game) || index >= MaxCabinets ||
      image[0] != 0x4d33u || image[1] != 0x5345u || image[2] != 0x4741u ||
      image[4] != 0xa319u || image[5] != EepromRevision ||
      (image[12] & 0xFFu) > 3 || (image[23] >> 8) >= MaxCabinets ||
      image[13] != PacketSize || image[3] != SettingsCRC(image))
    return false;
  image[12] = uint16_t((image[12] & 0xFF00u) | CommModeForCabinet(index));
  image[23] = uint16_t((index << 8) | (image[23] & 0xFFu));
  image[3] = SettingsCRC(image);
  return true;
}

} // namespace Dirtdvls

#endif
