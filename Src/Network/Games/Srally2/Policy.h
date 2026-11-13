// Exact-ROM Sega Rally 2 (srally2) link policy. SPDX-License-Identifier: GPL-3.0-or-later
//
// Exact-ROM EEPROM role policy; only srally2 is supported.
#ifndef INCLUDED_SRALLY2_NETWORK_POLICY_H
#define INCLUDED_SRALLY2_NETWORK_POLICY_H
#include "ResearchBuild.h"
#include <array>
#include <cstdint>
#include <string>

namespace Srally2 {
inline uint64_t ProtocolGUID(const std::string&,bool eight=false) { return Srally2Research::LinkGuid(eight); }
using EepromImage = std::array<uint16_t, 64>;

// The exact set verified through the relay; clones have not been verified.
inline bool IsSupportedGame(const std::string& game) {
  return game == "srally2";
}

// Preserve ring self-echo because native selection barriers inspect returning
// slots: the local slot is filled from the returning ring echo, the game sees
// at most one segment per slot per frame (CSimNetBoard publication rule), and
// the UDP lane never replaces the TCP ring. Hops are still forwarded by the
// receive thread or fanned out by the relay like every other timed-segment game.
inline bool PreserveRingSelfEcho(const std::string& game) {
  return IsSupportedGame(game);
}

constexpr unsigned MaxCabinets = Srally2Research::Capacity;   // buffer/decoder ceiling; selected-room capacity is 4 or 8
constexpr unsigned LinkTypeShift = 22;
constexpr uint32_t LinkTypeMask = 0xFu << LinkTypeShift;

inline uint16_t SettingsCRC(const EepromImage& image) {
  uint16_t crc = 0xFFFF;
  for (unsigned i = 8; i < 64; ++i) {
    for (unsigned shift : {8u, 0u}) {
      crc ^= uint16_t(((image[i] >> shift) & 0xFF) << 8);
      for (unsigned bit = 0; bit < 8; ++bit)
        crc = uint16_t((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0));
    }
  }
  return uint16_t(~crc);
}

// LINK TYPE for a zero-based cabinet index: 0 -> 1 (MASTER/CAR 1),
// 1 -> 2, 2 -> 3, 3 -> 4 (SLAVE/CAR 2..4).
inline unsigned LinkTypeForCabinet(unsigned index) { return index + 1; }

// Configure only a valid native EEPROM image; wait for ROM initialization otherwise.
inline bool ConfigureRelayEeprom(EepromImage& image, unsigned index, const std::string& game,bool eight=false) {
  if (!IsSupportedGame(game) || index >= Srally2Research::CabinetCapacity(eight)) return false;
  if (image[0] != 0x4d33 || image[1] != 0x5345 || image[2] != 0x4741) return false;  // "M3SEGA"
  if (image[5] != SettingsCRC(image)) return false;
  const uint32_t word = (uint32_t(image[12]) << 16) | image[13];
  const uint32_t patched = (word & ~LinkTypeMask) | ((LinkTypeForCabinet(index) & 0xF) << LinkTypeShift);
  if (patched == word) return true;  // already configured (native NVRAM is reused)
  image[12] = uint16_t(patched >> 16);
  image[13] = uint16_t(patched & 0xFFFF);
  image[5] = SettingsCRC(image);
  (void)word;
  return true;
}
}
#endif
