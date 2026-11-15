// Synthetic, ROM-free Dirt Devils (dirtdvls) 3584-byte (0x0E00) link packets
// matching the TX image at RAM 0x224A00 (CROM 0x030730/0x0307C0: header,
// human pose, MASTER AI records, CRC-16 at +0xD9C).
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_TEST_DIRTDVLS_PACKET_H
#define INCLUDED_TEST_DIRTDVLS_PACKET_H

#include "Network/Games/Dirtdvls/Prediction.h"
#include <array>
#include <cstdint>

inline Dirtdvls::EepromImage MakeDirtdvlsFactoryEeprom() {
  // Exact 64-word 93C46 image written by the native dirtdvls boot and read
  // back from the real ROM (words 0..35); word 3 = CCITT over words 6..34.
  Dirtdvls::EepromImage e{};
  const uint16_t words[36] = {
    0x4d33, 0x5345, 0x4741, 0x99b5, 0xa319, 0x0299, 0x0001, 0x0701,
    0x0101, 0x0101, 0x0101, 0x0000, 0x0201, 0x0e00, 0x0305, 0x0480,
    0x0100, 0x0000, 0x0000, 0x30d0, 0x30d0, 0x30d0, 0x8003, 0x0001,
    0x0202, 0x0200, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0001
  };
  for (unsigned i = 0; i < 36; ++i) e[i] = words[i];
  return e;
}

inline void WriteDirtdvlsPose(std::array<uint8_t, Dirtdvls::PacketSize>& p, size_t off,
                              float x, float y, float z,
                              int16_t pitch, int16_t yaw, int16_t roll) {
  Dirtdvls::WriteFloat(p.data(), off + 0x00, x);
  Dirtdvls::WriteFloat(p.data(), off + 0x04, y);
  Dirtdvls::WriteFloat(p.data(), off + 0x08, z);
  Dirtdvls::Write32(p.data(), off + 0x0C, uint16_t(pitch));
  Dirtdvls::Write32(p.data(), off + 0x10, uint16_t(yaw));
  Dirtdvls::Write32(p.data(), off + 0x14, uint16_t(roll));
}

// Racing packet (state 01 04 00 02) from cabinet `cab` of `n`; the MASTER
// (cabinet 0) also carries 8-n AI records at the native grid slots.
inline std::array<uint8_t, Dirtdvls::PacketSize> MakeDirtdvlsRacePacket(
    unsigned cab, unsigned n, uint32_t frame, float x, float y, float z,
    int16_t pitch, int16_t yaw, int16_t roll, uint32_t raceTimer = 0x0B7C,
    uint32_t state = 0x01040002u) {
  std::array<uint8_t, Dirtdvls::PacketSize> p{};
  Dirtdvls::Write32(p.data(), Dirtdvls::StateOffset, state);
  Dirtdvls::Write32(p.data(), 0x04, 0x00800202u);
  Dirtdvls::Write32(p.data(), 0x08, 0x02000000u);
  Dirtdvls::Write32(p.data(), 0x18, ((cab == 0 ? 1u : 3u) << 24) | (2u << 16));
  Dirtdvls::Write32(p.data(), Dirtdvls::FrameOffset, frame + 2);
  Dirtdvls::Write32(p.data(), Dirtdvls::SequenceOffset, frame);
  Dirtdvls::Write32(p.data(), 0x28, 0x01000000u | (uint32_t(cab) << 8) | (cab ? 1u : 0u));
  Dirtdvls::Write32(p.data(), Dirtdvls::RaceTimerOffset, raceTimer);
  WriteDirtdvlsPose(p, Dirtdvls::HumanPoseOffset, x, y, z, pitch, yaw, roll);
  Dirtdvls::WriteFloat(p.data(), Dirtdvls::HumanShadowOffset + 0x00, x + 8.0f);
  Dirtdvls::WriteFloat(p.data(), Dirtdvls::HumanShadowOffset + 0x04, y - 2.0f);
  Dirtdvls::WriteFloat(p.data(), Dirtdvls::HumanShadowOffset + 0x08, z + 15.6f);
  if (cab == 0) {
    const unsigned cpus = n < 8 ? 8 - n : 0;
    for (unsigned i = 0; i < cpus && i < Dirtdvls::MaxCpuCars; ++i) {
      const size_t rec = Dirtdvls::CpuRecordBase + i * Dirtdvls::CpuRecordStride;
      Dirtdvls::WriteFloat(p.data(), rec + 0x00, -1430.0f - 100.0f * float(i));
      Dirtdvls::WriteFloat(p.data(), rec + 0x04, 12.0f);
      Dirtdvls::WriteFloat(p.data(), rec + 0x08, 123.7f);
      WriteDirtdvlsPose(p, rec + Dirtdvls::CpuPoseOffset,
                        x - 40.0f * float(i + 1), y + 2.0f, z + 1000.0f + 25.0f * float(i),
                        int16_t(pitch + 5 * int(i)), int16_t(yaw - 90 * int(i)), int16_t(roll));
    }
  }
  // Rank table image (one-based) and the native "present" flag at +0x0F.
  for (unsigned i = 0; i < 8; ++i) Dirtdvls::Write16(p.data(), 0xA30 + 2 * i, uint16_t(i + 1));
  Dirtdvls::Write16(p.data(), 0x0E, 0x0001);
  Dirtdvls::SealPacket(p.data());
  return p;
}

#endif
