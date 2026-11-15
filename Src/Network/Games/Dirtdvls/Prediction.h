// Dirt Devils (dirtdvls) receive-copy 3D pose + angle prediction.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Exact-ROM packet layout (TX image RAM 0x224A00, 0xDA0 bytes + CRC-16 at
// +0xD9C, copied to net RAM 0xC0000100 by CROM 0x0307C0). Prediction only
// modifies validated local receive copies; it never touches control words.
#ifndef INCLUDED_DIRTDVLS_PREDICTION_H
#define INCLUDED_DIRTDVLS_PREDICTION_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "Policy.h"
#include "../Lemans24/Prediction.h"

namespace Dirtdvls {

using Lemans24::Read8;
using Lemans24::Read16;
using Lemans24::Read32;
using Lemans24::Write16;
using Lemans24::Write32;
using Lemans24::ReadFloat;
using Lemans24::WriteFloat;

// Game state word RAM 0x21AFA8 mirrored at TX+0x00:
// byte 0 = 0 attract / 1 credited game, byte 1 = step inside the scene,
// byte 2 = attract scene, byte 3 = game scene (0 car select, 1 course select,
// 2 race, 3 results, 4 continue). Race scene steps: 1 course intro, 2-3 fade,
// 4 grid/countdown/racing, 5 finished. RAM 0x228EA0 (race substate) stays 0
// on the grid before the countdown and becomes nonzero once it starts.
constexpr size_t StateOffset = 0x00;
constexpr size_t FrameOffset = 0x1C;
constexpr size_t SequenceOffset = 0x20;
constexpr size_t MachineOffset = 0x2A;  // link struct +0x08 low byte (0-based)
constexpr size_t RaceTimerOffset = 0x2C;
constexpr size_t HumanPoseOffset = 0x34;   // position xyz; angles follow at +0x0C
constexpr size_t HumanShadowOffset = 0x60; // second position kept at a fixed offset
constexpr size_t CpuRecordBase = 0x120;    // master only: 7 AI car records
constexpr size_t CpuRecordStride = 0x140;
constexpr size_t CpuPoseOffset = 0x54;     // position xyz; angles at +0x0C
constexpr unsigned MaxCpuCars = 7;
constexpr size_t ActivePayloadSize = 0xDA0;
constexpr size_t CrcOffset = 0xD9C;  // CRC-16 over bytes 0..0xD9B (CROM 0x05E878)
constexpr uint64_t MaxTimingUncertaintyUs = 60000000ULL;

constexpr unsigned GameScene_CarSelect = 0, GameScene_CourseSelect = 1,
                   GameScene_Race = 2, GameScene_Results = 3;
constexpr unsigned RaceStep_Grid = 4, RaceStep_Finished = 5;

struct StateWord {
  uint8_t mode = 0, step = 0, attractScene = 0, gameScene = 0;
};
inline constexpr StateWord DecodeState(uint32_t w) {
  return {uint8_t(w >> 24), uint8_t(w >> 16), uint8_t(w >> 8), uint8_t(w)};
}
inline constexpr bool InRaceScene(uint32_t w) {
  const StateWord s = DecodeState(w);
  return s.mode == 1u && s.gameScene == GameScene_Race;
}
inline constexpr bool Racing(uint32_t w) {
  return InRaceScene(w) && DecodeState(w).step == RaceStep_Grid;
}

// Same CCITT library routine as the EEPROM settings checksum: seed 0xDEBDEB00,
// byte stride 1, result = (acc >> 8) & 0xFFFF. Receivers (CROM 0x030694)
// discard every segment whose stored CRC does not match, so any edited
// receive copy must carry a recomputed CRC.
inline uint16_t PacketCRC(const uint8_t* p) {
  uint32_t acc = 0xDEBDEB00u;
  for (size_t i = 0; i < CrcOffset; ++i) {
    const uint16_t c = Detail::CcittByte(uint8_t(acc >> 24));
    acc = (((acc ^ (uint32_t(c) << 8)) | uint32_t(Read8(p, i))) << 8);
  }
  const uint16_t c = Detail::CcittByte(uint8_t(acc >> 24));
  acc ^= (uint32_t(c) << 8);
  return uint16_t((acc >> 8) & 0xFFFFu);
}
inline bool PacketCRCValid(const uint8_t* p) { return Read16(p, CrcOffset) == PacketCRC(p); }
inline void SealPacket(uint8_t* p) { Write16(p, CrcOffset, PacketCRC(p)); }

struct HeaderSample {
  uint32_t state = 0;
  uint32_t sequence = 0;
  int32_t senderIndex = -1;
  uint8_t seq7 = 0;
  unsigned numCpuCars = 0;
};

inline bool DecodeHeader(const uint8_t* raw, size_t size, unsigned expectedCabinetId,
                         unsigned localCabinetId, unsigned numMachines, HeaderSample& hdr) {
  if (!raw || size != PacketSize || numMachines < 2 || numMachines > MaxCabinets ||
      expectedCabinetId >= numMachines || localCabinetId >= numMachines ||
      expectedCabinetId == localCabinetId)
    return false;
  hdr.state = Read32(raw, StateOffset);
  hdr.sequence = Read32(raw, SequenceOffset);
  hdr.senderIndex = int32_t(Read8(raw, MachineOffset));
  hdr.seq7 = uint8_t(hdr.sequence & 0x7Fu);
  if (!Racing(hdr.state) || hdr.senderIndex != int32_t(expectedCabinetId) ||
      !PacketCRCValid(raw))
    return false;
  // Only the MASTER (cabinet 0) carries the AI grid; slaves send zero records.
  hdr.numCpuCars = expectedCabinetId == 0 ? MaxCpuCars : 0;
  return true;
}

// Pose records store the three Euler angles as zero-extended 16-bit values in
// 32-bit words right after the position.
inline bool DecodeCarPose(const uint8_t* raw, size_t poseOffset, Lemans24::PoseSample& out) {
  if (!raw || poseOffset + 0x18 > ActivePayloadSize) return false;
  const float fx = ReadFloat(raw, poseOffset + 0x00);
  const float fy = ReadFloat(raw, poseOffset + 0x04);
  const float fz = ReadFloat(raw, poseOffset + 0x08);
  if (!std::isfinite(fx) || !std::isfinite(fy) || !std::isfinite(fz)) return false;
  const uint32_t a0 = Read32(raw, poseOffset + 0x0C), a1 = Read32(raw, poseOffset + 0x10),
                 a2 = Read32(raw, poseOffset + 0x14);
  if ((a0 | a1 | a2) & 0xFFFF0000u) return false;
  out.modelId = 0;
  out.pitch = int16_t(uint16_t(a0));
  out.yaw = int16_t(uint16_t(a1));
  out.roll = int16_t(uint16_t(a2));
  // Empty AI records (slave packets, unused grid slots) are never predicted.
  if (fx == 0.0f && fy == 0.0f && fz == 0.0f && !a0 && !a1 && !a2) return false;
  out.x = double(fx);
  out.y = double(fy);
  out.z = double(fz);
  out.vx = out.vy = out.vz = 0.0;  // chord velocity only
  if (std::abs(out.x) > 500000.0 || std::abs(out.y) > 500000.0 || std::abs(out.z) > 500000.0)
    return false;
  return true;
}

inline void WriteCarPose(uint8_t* out, size_t poseOffset, const Lemans24::PoseOutput& p) {
  WriteFloat(out, poseOffset + 0x00, p.x);
  WriteFloat(out, poseOffset + 0x04, p.y);
  WriteFloat(out, poseOffset + 0x08, p.z);
  Write32(out, poseOffset + 0x0C, uint16_t(p.pitch));
  Write32(out, poseOffset + 0x10, uint16_t(p.yaw));
  Write32(out, poseOffset + 0x14, uint16_t(p.roll));
}

struct Result {
  bool human = false;
  unsigned cpus = 0;
};

class Predictor {
  unsigned m_tickRateHz = 60;
  bool m_active = false;
  int32_t m_senderIndex = -1;
  Lemans24::PoseMotion m_human;
  std::array<Lemans24::PoseMotion, MaxCpuCars> m_cpus;

public:
  Predictor() { SetTickRate(60); }

  void SetTickRate(unsigned hz) {
    m_tickRateHz = std::clamp(hz, 5u, 60u);
    m_human.SetTickRate(m_tickRateHz);
    for (auto& c : m_cpus) c.SetTickRate(m_tickRateHz);
  }

  void Reset() {
    m_active = false;
    m_senderIndex = -1;
    m_human.Reset();
    for (auto& c : m_cpus) c.Reset();
  }

  Result Apply(const uint8_t* raw, size_t size, uint8_t* out, uint64_t localFrame,
               double sampleAgeFrames, double uncertaintyFrames, uint64_t sourceTimestampUs,
               unsigned expectedCabinetId, unsigned localCabinetId, unsigned numMachines) {
    Result res{};
    if (!out) {
      Reset();
      return res;
    }
    HeaderSample hdr{};
    if (!DecodeHeader(raw, size, expectedCabinetId, localCabinetId, numMachines, hdr)) {
      if (m_active) Reset();
      return res;
    }
    if (!m_active || hdr.senderIndex != m_senderIndex) {
      Reset();
      m_active = true;
      m_senderIndex = hdr.senderIndex;
    }

    Lemans24::PoseSample humanSample{};
    Lemans24::PoseOutput humanOut{};
    if (DecodeCarPose(raw, HumanPoseOffset, humanSample)) {
      if (m_human.Apply(humanSample, hdr.seq7, sourceTimestampUs, localFrame,
                        sampleAgeFrames, uncertaintyFrames, humanOut, false)) {
        WriteCarPose(out, HumanPoseOffset, humanOut);
        // Keep the shadow position at its native offset from the car.
        const float sx = ReadFloat(raw, HumanShadowOffset + 0x00) - float(humanSample.x);
        const float sy = ReadFloat(raw, HumanShadowOffset + 0x04) - float(humanSample.y);
        const float sz = ReadFloat(raw, HumanShadowOffset + 0x08) - float(humanSample.z);
        if (std::isfinite(sx) && std::isfinite(sy) && std::isfinite(sz) &&
            std::abs(sx) < 64.0f && std::abs(sy) < 64.0f && std::abs(sz) < 64.0f) {
          WriteFloat(out, HumanShadowOffset + 0x00, humanOut.x + sx);
          WriteFloat(out, HumanShadowOffset + 0x04, humanOut.y + sy);
          WriteFloat(out, HumanShadowOffset + 0x08, humanOut.z + sz);
        }
        res.human = true;
      }
    } else {
      m_human.Reset();
    }

    for (unsigned i = 0; i < hdr.numCpuCars; ++i) {
      const size_t cpuOffset = CpuRecordBase + size_t(i) * CpuRecordStride + CpuPoseOffset;
      Lemans24::PoseSample cpuSample{};
      Lemans24::PoseOutput cpuOut{};
      if (DecodeCarPose(raw, cpuOffset, cpuSample)) {
        if (m_cpus[i].Apply(cpuSample, hdr.seq7, sourceTimestampUs, localFrame,
                            sampleAgeFrames, uncertaintyFrames, cpuOut, false)) {
          WriteCarPose(out, cpuOffset, cpuOut);
          ++res.cpus;
        }
      } else {
        m_cpus[i].Reset();
      }
    }
    if (res.human || res.cpus) SealPacket(out);
    return res;
  }
};

struct State {
  unsigned tickRateHz = 60;
  std::vector<Predictor> predictors;
  std::array<uint32_t, MaxCabinets - 1> lastPhase = {~0u, ~0u, ~0u};
  bool humanLogged = false;
  bool cpuLogged = false;

  void SetTickRate(unsigned hz) {
    tickRateHz = std::clamp(hz, 5u, 60u);
    for (auto& p : predictors) p.SetTickRate(tickRateHz);
  }
  void Reset() {
    const unsigned hz = tickRateHz;
    *this = State();
    tickRateHz = hz;
  }
};

} // namespace Dirtdvls

#endif
