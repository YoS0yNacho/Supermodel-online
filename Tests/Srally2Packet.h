// Synthetic, ROM-free Sega Rally 2 (srally2) 2048-byte packets matching CROM
// 0xFF8406F0..0xFF8407EC (TX header/poses) and 0xFF853E98 (Pose Struct).
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_TEST_SRALLY2_PACKET_H
#define INCLUDED_TEST_SRALLY2_PACKET_H
#include "Network/Games/Srally2/Prediction.h"
#include <array>
#include <cmath>
#include <cstdint>

inline void WriteSrally2Pose(std::array<uint8_t, Srally2::PacketSize>& p, size_t off,
                             float x, float y, float z, float vx, float vy, float vz,
                             int16_t pitch, int16_t yaw, int16_t roll,
                             float yawRateRadPerFrame, uint32_t carSlotIndex) {
  Srally2::WriteFloat(p.data(), off + 0x00, x);
  Srally2::WriteFloat(p.data(), off + 0x04, y);
  Srally2::WriteFloat(p.data(), off + 0x08, z);
  Srally2::WriteFloat(p.data(), off + 0x0C, vx);
  Srally2::WriteFloat(p.data(), off + 0x10, vy);
  Srally2::WriteFloat(p.data(), off + 0x14, vz);
  Srally2::Write16(p.data(), off + 0x18, uint16_t(pitch));
  Srally2::Write16(p.data(), off + 0x1A, uint16_t(yaw));
  Srally2::Write16(p.data(), off + 0x1C, uint16_t(roll));
  // Non-editable static car tag (+0x44), angular rate (+0x4C), dust/smoke flags (+0x50),
  // and wheel/suspension words (+0x54..+0x6B) for byte-invariance assertions.
  Srally2::Write32(p.data(), off + 0x44, ((carSlotIndex + 1u) << 14) | 0x20000000u);
  Srally2::WriteFloat(p.data(), off + 0x4C, yawRateRadPerFrame);
  Srally2::Write32(p.data(), off + 0x50, 0x000500A3u + carSlotIndex);
  for (size_t w = 0x54; w < 0x80; w += 4)
    Srally2::Write32(p.data(), off + w, uint32_t(0x55AA0000u | (carSlotIndex << 8) | w));
}

inline std::array<uint8_t, Srally2::PacketSize> MakeSrally2RacePacket(
    unsigned cabinetId, uint32_t raceTimer, float x, float y, float z,
    float vx, float vy, float vz, int16_t pitch, int16_t yaw, int16_t roll,
    float yawRateRadPerFrame, unsigned numCpuCars = 2, uint32_t linkPhase = 10u) {
  std::array<uint8_t, Srally2::PacketSize> p{};
  Srally2::Write32(p.data(), 0x00, cabinetId);
  Srally2::Write32(p.data(), 0x04, cabinetId); // playableIndex 0..N-1
  Srally2::Write32(p.data(), 0x08, 1u);        // gameMode = in-race
  Srally2::Write32(p.data(), 0x0C, 1u);        // gameSubMode = active race
  Srally2::Write32(p.data(), 0x10, 2u);        // raceStage (stage 2)
  Srally2::Write32(p.data(), 0x18, linkPhase); // 10 = race/countdown, 11 = post-race
  Srally2::Write32(p.data(), 0x20, cabinetId % 8u);
  // Ring barrier words (+0x2C, +0x44, +0x6C) that prediction must NEVER touch.
  Srally2::Write32(p.data(), 0x2C, 0x00000003u);
  Srally2::Write32(p.data(), 0x44, 0x12345678u + cabinetId);
  Srally2::Write32(p.data(), 0x6C, 0x12345678u);
  Srally2::Write32(p.data(), 0x74, raceTimer);
  Srally2::Write32(p.data(), 0xC4, numCpuCars);

  WriteSrally2Pose(p, Srally2::HumanPoseOffset, x, y, z, vx, vy, vz,
                   pitch, yaw, roll, yawRateRadPerFrame, cabinetId);
  for (unsigned c = 0; c < numCpuCars && c < Srally2::MaxCpuCars; ++c) {
    const float cx = x - float((c + 1) * 12.0f) + 0.65f * float(raceTimer);
    const float cy = y + 0.1f * float(c);
    const float cz = z + float((c + 1) * 3.0f);
    WriteSrally2Pose(p, Srally2::CpuPoseBase + size_t(c) * Srally2::PoseStride,
                     cx, cy, cz, vx + 0.65f, vy, vz, pitch, yaw, roll,
                     yawRateRadPerFrame, 8u + c);
  }
  return p;
}
#endif
