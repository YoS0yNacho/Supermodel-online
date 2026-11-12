// Exact-ROM Le Mans 24 (lemans24, Japan Revision B) async policy.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_LEMANS24_NETWORK_POLICY_H
#define INCLUDED_LEMANS24_NETWORK_POLICY_H

#include <array>
#include <cstdint>
#include <string>

namespace Lemans24 {

constexpr unsigned PacketSize = 0x0CC0;        // 3264-byte NetBoard segment (RAM16[0x204] / RAM16[0x1228])
constexpr unsigned ActivePayloadSize = 0x0B20; // 2848-byte DMA payload copied by 0x0F8FB0 / 0x0F8FF0
constexpr unsigned MaxCabinets = 6;            // otherCount in 0..5 -> 1..6 linked cabinets (0x08B410)
constexpr unsigned MaxCpuCars = 6;             // {0, 6, 5, 4, 3, 3} AI cars per cabinet for 1..6 cabinets

inline bool IsSupportedGame(const std::string& game) {
  return game == "lemans24";
}

inline uint64_t ProtocolGUID(const std::string& game) {
  return game == "lemans24" ? 0x5bf177da3487aULL : 0ULL;
}

// Native service/RAM 0x1227 comm. mode: 0 = MASTER, 1 = SLAVE, 2 = SATELLITE (solo default).
inline constexpr uint8_t CommModeForCabinet(unsigned index) {
  return index == 0 ? 0u : 1u;
}

inline constexpr unsigned CpuCountForCabinets(unsigned numMachines) {
  switch (numMachines) {
    case 2: return 6;
    case 3: return 5;
    case 4: return 4;
    case 5: return 3;
    case 6: return 3;
    default: return 0;
  }
}

// RAM32[0x17E7E0] / TX[+0x830]: 0 = attract/demo, 1..3 = solo race, 5..8 = linked multiplayer race.
inline constexpr bool IsLinkedRaceMode(uint32_t raceMode) {
  return raceMode >= 5u && raceMode <= 8u;
}

// Valid Challenger Entry session tags in TX[+0x08] / TX[+0x88A]:
// 4..5 = Sprint Link, 6..7 = Endurance Link (0x0D3ED0..0x0D411C).
inline constexpr bool IsChallengerSessionTag(uint32_t tag) {
  return tag >= 4u && tag <= 7u;
}

inline constexpr bool AtChallengerGate(uint32_t stage, uint32_t faultSub,
                                       uint32_t stageSub, uint16_t linkSel,
                                       uint32_t widget13Cb, uint32_t entryTimer) {
  // Gate the native Challenger Entry window after all cabinets reach the same phase.
  return stage == 1u && faultSub == 0u && stageSub == 8u && linkSel != 0u &&
         widget13Cb == 0x000D3C74u && entryTimer == 0x00000280u;
}

inline constexpr bool AtStartGate(uint32_t stage, uint32_t faultSub,
                                  uint32_t raceMode, uint32_t stageSub) {
  // Gate before the linked-race countdown after all native grid actors are installed.
  return stage == 3u && faultSub == 0u && IsLinkedRaceMode(raceMode) && stageSub == 2u;
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

// Exact-ROM checksum over EEPROM words 6–34.
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

inline bool ConfigureRelayEeprom(EepromImage& image, unsigned index, const std::string& game) {
  if (!IsSupportedGame(game) || index >= MaxCabinets ||
      image[0] != 0x4d33u || image[1] != 0x5345u || image[2] != 0x4741u ||
      image[4] != 0xa319u || image[5] != 0x015fu ||
      image[6] > 0x270fu || image[13] != PacketSize ||
      image[3] != SettingsCRC(image))
    return false;

  // EEPROM word 12 stores comm mode; word 41 mirrors it in the backup copy.
  const uint8_t role = CommModeForCabinet(index);
  image[12] = uint16_t((image[12] & 0xFF00u) | role);
  image[41] = image[12];
  image[3] = SettingsCRC(image);
  return true;
}

} // namespace Lemans24

#endif
