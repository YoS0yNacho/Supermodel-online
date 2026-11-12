// Synthetic, ROM-free Le Mans 24 (lemans24) 3264-byte (0x0CC0) Step 1.5 packets
// matching 0x088A40..0x08E500 (TX header/poses, Step 1.5 o^2 word swap).
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_TEST_LEMANS24_PACKET_H
#define INCLUDED_TEST_LEMANS24_PACKET_H

#include "Network/Games/Lemans24/Prediction.h"
#include <array>
#include <cmath>
#include <cstdint>

inline Lemans24::EepromImage MakeLemans24FactoryEeprom() {
  // Exact 64-word 93C46 image initialized by native lemans24 boot (0x0FD670),
  // whose 0x0FD610 checksum over words 6..34 equals 0x5D45 (or 0xC7EE with
  // default coin/bookkeeping counters).
  Lemans24::EepromImage e{};
  e[0] = 0x4d33;
  e[1] = 0x5345;
  e[2] = 0x4741;
  e[4] = 0xa319;
  e[5] = 0x015f;
  const uint16_t defaults[28] = {
    0x0501, 0x0101, 0x0101, 0x0101, 0x0000, 0x0002, 0x0cc0, 0x0005,
    0x0070, 0x0100, 0x0000, 0x0000, 0x0403, 0x0703, 0x0100, 0x0106,
    0x0001, 0x0401, 0x0400, 0x0001, 0x0e09, 0x0802, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000
  };
  e[6] = 0x0000;
  for (unsigned k = 0; k < 28; ++k) {
    e[7 + k] = defaults[k];
    e[36 + k] = defaults[k];
  }
  e[3] = Lemans24::SettingsCRC(e);
  return e;
}

inline void WriteLemans24CarRecord(std::array<uint8_t, Lemans24::PacketSize>& p,
                                   size_t off, int16_t modelId,
                                   float x, float y, float z,
                                   float vx, float vy, float vz,
                                   int16_t pitch, int16_t yaw, int16_t roll,
                                   unsigned tagSeed) {
  Lemans24::Write16(p.data(), off + 0x00, uint16_t(modelId));
  Lemans24::Write16(p.data(), off + 0x02, uint16_t(pitch));
  Lemans24::Write16(p.data(), off + 0x04, uint16_t(yaw));
  Lemans24::Write16(p.data(), off + 0x06, uint16_t(roll));
  Lemans24::WriteFloat(p.data(), off + 0x08, x);
  Lemans24::WriteFloat(p.data(), off + 0x0C, y);
  Lemans24::WriteFloat(p.data(), off + 0x10, z);
  const float spdUnits = std::sqrt(vx * vx + vy * vy + vz * vz);
  Lemans24::WriteFloat(p.data(), off + 0x1C, spdUnits * 21.6f); // exact 21.6x world units/frame -> km/h
  Lemans24::WriteFloat(p.data(), off + 0x20, vx);
  Lemans24::WriteFloat(p.data(), off + 0x24, vy);
  Lemans24::WriteFloat(p.data(), off + 0x28, vz);
  // Relative wheel/shadow/effect words that prediction must leave byte-identical.
  for (size_t w = 0x2C; w < Lemans24::PoseStride; w += 4)
    Lemans24::Write32(p.data(), off + w, uint32_t(0xA55A0000u | (tagSeed << 8) | w));
}

inline std::array<uint8_t, Lemans24::PacketSize> MakeLemans24RacePacket(
    unsigned cabinetId, unsigned numMachines, uint32_t frame,
    float x, float y, float z, float vx, float vy, float vz,
    int16_t pitch, int16_t yaw, int16_t roll,
    uint32_t raceSubstate = 3u, uint32_t raceSub = 4u, uint32_t raceMode = 5u) {
  std::array<uint8_t, Lemans24::PacketSize> p{};
  Lemans24::Write32(p.data(), 0x00, raceSubstate);
  Lemans24::Write32(p.data(), 0x08, 1u); // validFlag == 1
  Lemans24::Write32(p.data(), 0x1C, 0x00000001u); // barrierReady (immutable)
  Lemans24::Write32(p.data(), 0x20, raceSub);
  Lemans24::Write32(p.data(), 0x28, cabinetId);
  Lemans24::Write32(p.data(), 0x2C, Lemans24::CommModeForCabinet(cabinetId));
  Lemans24::Write32(p.data(), 0x830, raceMode);
  p[0x850 ^ 2] = uint8_t(frame & 0x7Fu);
  Lemans24::Write16(p.data(), 0x88A, 4u); // Sprint Link session tag

  WriteLemans24CarRecord(p, Lemans24::HumanPoseOffset, int16_t(cabinetId % 8u),
                         x, y, z, vx, vy, vz, pitch, yaw, roll, cabinetId + 1u);

  const unsigned cpuCount = Lemans24::CpuCountForCabinets(numMachines);
  for (unsigned k = 0; k < Lemans24::MaxCpuCars; ++k) {
    const size_t base = Lemans24::CpuPoseBase + size_t(k) * Lemans24::PoseStride;
    if (k < cpuCount) {
      const float cx = x - float((k + 1) * 35.0f) + 0.45f * float(frame);
      const float cy = y + 0.25f * float(k);
      const float cz = z + float((k + 1) * 8.0f);
      WriteLemans24CarRecord(p, base, int16_t(8 + k),
                             cx, cy, cz, vx + 0.45f, vy, vz,
                             pitch, int16_t(yaw + int16_t(k * 64)), roll, 16u + k);
    } else {
      WriteLemans24CarRecord(p, base, -1, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 32u + k);
    }
  }
  return p;
}

#endif
