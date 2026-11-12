// Synthetic packet fixture, not ROM code. GPL-3.0-or-later.
#pragma once
#include "Network/Games/Daytona2/Daytona2Prediction.h"
#include <array>
#include <cassert>
using Packet = std::array<uint8_t, Dayto2pe::PacketSize>;
inline void Word(Packet& p, size_t offset, uint32_t value)
{
  for (unsigned i = 0; i < 4; ++i) p[(offset + i) ^ 2] = uint8_t(value >> (24 - 8 * i));
}
inline void Checksum(Packet& p)
{
  uint32_t sum = 0;
  for (size_t offset = 4; offset < 0x64; offset += 4)
    if (offset != 0x3c) sum += Dayto2pe::Read32(p.data(), offset);
  Word(p, 0x3c, sum);
}
inline Packet MakePacket(uint16_t tick, float x, float z, const Dayto2pe::Profile& profile = Dayto2pe::PowerEdition)
{
  Packet p;
  // Opaque body/AI/control bytes deliberately nonzero to catch excess writes.
  for (size_t i = 0; i < p.size(); ++i) p[i] = uint8_t(i * 73 + 19);
  Word(p, 4, profile.raceState << profile.stateShift); Word(p, 8, tick);
  Word(p, profile.classOffset, (Dayto2pe::Read32(p.data(),profile.classOffset)&~profile.classMask)|profile.classValue);
  Word(p, 0x38, tick);
  Word(p, 0x64, 0x04041a81);
  Dayto2pe::WriteFloat(p.data(), 0x6c, x);
  Dayto2pe::WriteFloat(p.data(), 0x70, z);
  Checksum(p);
  return p;
}
inline void OnlyPositionsChanged(const uint8_t* raw, const uint8_t* out)
{
  for (size_t i = 0; i < Dayto2pe::PacketSize; ++i)
    if (i < 0x6c || i >= 0x74) assert(raw[i] == out[i]);
}
