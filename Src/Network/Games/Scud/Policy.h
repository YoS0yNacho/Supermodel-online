// Exact-ROM Scud/Scud Plus async policy. SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_SCUD_NETWORK_POLICY_H
#define INCLUDED_SCUD_NETWORK_POLICY_H
#include <array>
#include <cstdint>
#include <string>
namespace Scud {
constexpr unsigned PacketSize = 0x400, MaxCabinets = 8, MaxCars = 40;
inline bool IsSupportedGame(const std::string& game) { return game == "scud" || game == "scudplus"; }
inline uint64_t ProtocolGUID(const std::string& game) {
  return game == "scud" ? 0x5bf177da34876ULL : game == "scudplus" ? 0x5bf177da34877ULL : 0;
}
inline bool AtStartGate(unsigned current, unsigned next, unsigned sub) {
  // Independently disassembled: scud FFF2652C / scudplus FFF282D8.
  // Stage 18 has completed track/car setup; stage 19/sub 1 has NOT installed
  // rolling-start actors/cameras yet. Never hold at the later stage-21 GO.
  return current == 19 && next == 19 && sub == 1;
}
using EepromImage = std::array<uint16_t,64>;
inline uint16_t SettingsCRC(const EepromImage& image) {
  uint16_t crc = 0;
  for (unsigned i=6; i<35; ++i) for (unsigned shift : {8u,0u}) {
    crc ^= uint16_t((image[i] >> shift) & 255) << 8;
    for (unsigned bit=0; bit<8; ++bit) crc=uint16_t((crc<<1)^((crc&0x8000)?0x1021:0));
  }
  return crc;
}
inline bool ConfigureRelayEeprom(EepromImage& image, unsigned index, const std::string& game) {
  if (!IsSupportedGame(game) || index >= MaxCabinets || image[0]!=0x4d33 ||
      image[1]!=0x5345 || image[2]!=0x4741 || image[4]!=0xa218 ||
      image[5]!=(game=="scud" ? 0x0103 : 0x0105) || image[3]!=SettingsCRC(image)) return false;
  // Both native service tables map RAM 0x10011e to role (0 SINGLE,
  // 1 MASTER, 2 SLAVE) and 0x10011f to car INDEX 0..7, displayed as 1..8.
  // The ROM reads each EEPROM word with sth, so role is the HIGH byte.
  // Scud descriptor FFF9A514; Plus FFFCC798. Preserve unrelated settings.
  image[15]=uint16_t(((index==0 ? 1 : 2)<<8)|index);
  image[44]=image[15]; image[3]=SettingsCRC(image);
  return true;
}
}
#endif
