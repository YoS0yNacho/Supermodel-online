// Le Mans 24 (lemans24) receive-copy 3D pose + angle prediction.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Exact-ROM packet layout; prediction modifies only validated local receive copies.
#ifndef INCLUDED_LEMANS24_PREDICTION_H
#define INCLUDED_LEMANS24_PREDICTION_H

#include "Policy.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace Lemans24 {

constexpr size_t HumanPoseOffset = 0x044;
constexpr size_t CpuPoseBase = 0x120;
constexpr size_t PoseStride = 0x0A8;
constexpr uint64_t MaxTimingUncertaintyUs = 60000000ULL;

// PowerPC offsets are big-endian; HLE words swap 16-bit halves.
inline uint8_t Read8(const uint8_t* p, size_t o) { return p[o ^ 2]; }
inline uint16_t Read16(const uint8_t* p, size_t o) {
  return uint16_t((uint16_t(Read8(p, o)) << 8) | Read8(p, o + 1));
}
inline uint32_t Read32(const uint8_t* p, size_t o) {
  return (uint32_t(Read16(p, o)) << 16) | Read16(p, o + 2);
}
inline void Write16(uint8_t* p, size_t o, uint16_t v) {
  p[o ^ 2] = uint8_t(v >> 8);
  p[(o + 1) ^ 2] = uint8_t(v);
}
inline void Write32(uint8_t* p, size_t o, uint32_t v) {
  Write16(p, o, uint16_t(v >> 16));
  Write16(p, o + 2, uint16_t(v));
}
inline float ReadFloat(const uint8_t* p, size_t o) {
  static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
                "lemans24 poses require IEEE-754 binary32");
  const uint32_t bits = Read32(p, o);
  float f;
  std::memcpy(&f, &bits, sizeof(f));
  return f;
}
inline void WriteFloat(uint8_t* p, size_t o, float f) {
  uint32_t bits;
  std::memcpy(&bits, &f, sizeof(bits));
  Write32(p, o, bits);
}

inline constexpr bool InRaceSubstate(uint32_t substate) {
  return substate >= 2u && substate <= 4u;
}

struct HeaderSample {
  uint32_t raceSubstate = 0;
  uint32_t raceSub = 0;
  int32_t senderIndex = -1;
  uint32_t commMode = 0;
  uint32_t raceMode = 0;
  uint8_t seq7 = 0;
  unsigned numCpuCars = 0;
};

inline bool DecodeHeader(const uint8_t* raw, size_t size, unsigned expectedCabinetId,
                         unsigned localCabinetId, unsigned numMachines, HeaderSample& hdr) {
  if (!raw || size != PacketSize || numMachines < 2 || numMachines > MaxCabinets ||
      expectedCabinetId >= numMachines || localCabinetId >= numMachines ||
      expectedCabinetId == localCabinetId)
    return false;
  hdr.raceSubstate = Read32(raw, 0x00);
  const uint32_t validFlag = Read32(raw, 0x08);
  hdr.raceSub = Read32(raw, 0x20);
  hdr.senderIndex = int32_t(Read32(raw, 0x28));
  hdr.commMode = Read32(raw, 0x2C);
  hdr.raceMode = Read32(raw, 0x830);
  hdr.seq7 = uint8_t(Read8(raw, 0x850) & 0x7Fu);
  if (!InRaceSubstate(hdr.raceSubstate) || validFlag != 1u || hdr.raceSub > 7u ||
      hdr.senderIndex != int32_t(expectedCabinetId) ||
      hdr.commMode != CommModeForCabinet(expectedCabinetId) ||
      !IsLinkedRaceMode(hdr.raceMode))
    return false;
  hdr.numCpuCars = CpuCountForCabinets(numMachines);
  return true;
}

struct PoseSample {
  int16_t modelId = -1;
  int16_t pitch = 0, yaw = 0, roll = 0;
  double x = 0.0, y = 0.0, z = 0.0;
  double vx = 0.0, vy = 0.0, vz = 0.0;
};

inline bool DecodePose(const uint8_t* raw, size_t poseOffset, PoseSample& out) {
  if (!raw || poseOffset + PoseStride > ActivePayloadSize) return false;
  out.modelId = int16_t(Read16(raw, poseOffset + 0x00));
  if (out.modelId < 0 || out.modelId >= 64) return false;
  const float fx = ReadFloat(raw, poseOffset + 0x08);
  const float fy = ReadFloat(raw, poseOffset + 0x0C);
  const float fz = ReadFloat(raw, poseOffset + 0x10);
  const float fspd = ReadFloat(raw, poseOffset + 0x1C);
  const float fvx = ReadFloat(raw, poseOffset + 0x20);
  const float fvy = ReadFloat(raw, poseOffset + 0x24);
  const float fvz = ReadFloat(raw, poseOffset + 0x28);
  if (!std::isfinite(fx) || !std::isfinite(fy) || !std::isfinite(fz) ||
      !std::isfinite(fspd) || !std::isfinite(fvx) || !std::isfinite(fvy) ||
      !std::isfinite(fvz))
    return false;
  out.pitch = int16_t(Read16(raw, poseOffset + 0x02));
  out.yaw = int16_t(Read16(raw, poseOffset + 0x04));
  out.roll = int16_t(Read16(raw, poseOffset + 0x06));
  // Ignore empty records before native grid setup.
  if (out.modelId == 0 && fx == 0.0f && fy == 0.0f && fz == 0.0f &&
      fvx == 0.0f && fvy == 0.0f && fvz == 0.0f &&
      out.pitch == 0 && out.yaw == 0 && out.roll == 0 &&
      Read32(raw, poseOffset + 0x64) == 0u && Read32(raw, poseOffset + 0x68) == 0u)
    return false;
  out.x = double(fx);
  out.y = double(fy);
  out.z = double(fz);
  out.vx = double(fvx);
  out.vy = double(fvy);
  out.vz = double(fvz);
  if (std::abs(out.x) > 500000.0 || std::abs(out.y) > 500000.0 || std::abs(out.z) > 500000.0)
    return false;
  // 1 world unit/frame = 21.6 km/h (32.0 units/frame ~= 691 km/h).
  if (std::hypot(out.vx, out.vz) > 32.0 || std::abs(out.vy) > 12.0)
    return false;
  return true;
}

struct PoseOutput {
  float x = 0.0f, y = 0.0f, z = 0.0f;
  int16_t pitch = 0, yaw = 0, roll = 0;
};

class PoseMotion {
  using Vec2 = std::array<double, 2>;
  using Vec3 = std::array<double, 3>;

  static constexpr double kMaxSpeedXZ = 32.0; // world units / 60 Hz frame (~691 km/h)
  static constexpr double kMaxSpeedY = 12.0;

  double m_intervalFrames = 1.0;
  double m_jitterFrames = 0.0;
  // Track sustained sample age separately from arrival jitter.
  double m_latencyFrames = 0.0;
  double m_activeTau = 6.0;
  bool m_seeded = false;
  bool m_moving = false;
  bool m_hasPrevNativeXZ = false;
  int16_t m_modelId = -1;
  uint8_t m_lastSeq7 = 0;
  uint64_t m_lastSourceUs = 0;
  uint64_t m_receivedFrame = 0;
  uint64_t m_lastEnvelopeFrame = 0, m_lastEnvelopeSourceUs = 0;
  uint8_t m_lastEnvelopeSeq7 = 0;
  double m_lastDt = 1.0;
  double m_age = 0.0;
  double m_omega = 0.0;

  Vec3 m_pos{}, m_lastNativeVelocity{};
  Vec2 m_velXZ{}, m_chordXZ{}, m_prevNativeXZ{};
  double m_velY = 0.0;
  double m_pitch16 = 0.0, m_yaw16 = 0.0, m_roll16 = 0.0;
  double m_pitchRate16 = 0.0, m_yawRate16 = 0.0, m_rollRate16 = 0.0;

  Vec3 m_posCorr{}, m_velCorr{};
  Vec3 m_angCorr{};
  double m_yawVelCorr = 0.0;

  static double Norm2(const Vec2& v) { return std::hypot(v[0], v[1]); }
  static Vec2 Rotate2(const Vec2& v, double a) {
    const double c = std::cos(a), s = std::sin(a);
    return {v[0] * c - v[1] * s, v[0] * s + v[1] * c};
  }
  static double WrapAngle16(double d) {
    d = std::fmod(d, 65536.0);
    if (d >= 32768.0) d -= 65536.0;
    if (d < -32768.0) d += 65536.0;
    return d;
  }
  static int16_t PackAngle16(double a) {
    const long long q = std::llround(a);
    return int16_t(uint16_t(static_cast<unsigned long long>(q) & 0xFFFFULL));
  }
  static double SoftCap(double x, double cap, double knee = 2.0) {
    if (x <= 0.0) return 0.0;
    const double th = std::max(0.0, cap - knee);
    return (x <= th || knee <= 1e-6) ? std::min(x, cap) : (cap - knee * std::exp(-(x - th) / knee));
  }

  bool SmoothMode() const { return m_intervalFrames > 1.001 || m_jitterFrames > 0.25; }
  bool LatencyExtended() const { return m_latencyFrames > 12.0; }
  double MaxFrames() const {
    if (!SmoothMode()) return LatencyExtended() ? std::min(36.0, m_latencyFrames * 1.25) : 12.0;
    // Extend low-rate prediction by measured one-way sample age.
    const double base = std::min(42.0, 12.0 + 1.8 * (m_intervalFrames - 1.0) + 1.8 * m_jitterFrames +
                                      (m_jitterFrames > 0.25 ? 10.0 : 0.0));
    return std::max(base, LatencyExtended() ? std::min(36.0, m_latencyFrames * 1.25) : 0.0);
  }
  double LimitXZ() const {
    if (!SmoothMode()) return LatencyExtended() ? std::max(40.0, MaxFrames() * 20.0) : 40.0;
    return std::max(40.0, MaxFrames() * 20.0);
  }
  double SnapXZ(double dt) const {
    if (!SmoothMode()) return 48.0;
    return 48.0 + 20.0 * (m_intervalFrames - 1.0) +
           (m_jitterFrames > 0.25 ? 16.0 * std::min(dt, 45.0) : 0.0);
  }
  double Tau() const { return std::max(6.0, m_intervalFrames * 1.25 + 0.35 * m_jitterFrames +
      (LatencyExtended() ? 0.15 * (m_latencyFrames - 12.0) : 0.0) +
      (m_latencyFrames > 18.0 ? 0.35 * (m_latencyFrames - 18.0) : 0.0)); }

  double EffectiveFrames(double elapsed) const {
    const double mf = MaxFrames();
    const double knee = (m_jitterFrames > 0.25) ? std::min(8.0, 0.25 * mf) : 2.0;
    return (!SmoothMode() && m_intervalFrames <= 1.001)
               ? std::min(MaxFrames(), m_age + elapsed)
               : SoftCap(m_age + elapsed, mf, knee);
  }

  Vec2 LeadXZ(const Vec2& v, double frames) const {
    if (frames <= 0.0) return {0.0, 0.0};
    Vec2 disp = v;
    if (std::abs(m_omega) > 1e-6) {
      // Preserve curved motion and damp long, low-confidence projections.
      const bool farRelay = LatencyExtended();
      const double freeH = farRelay ? std::min(24.0, m_intervalFrames + 6.0 + (m_latencyFrames - 12.0) * 0.5) : std::min(16.0, m_intervalFrames + 6.0);
      const double dampScale = (farRelay ? 8.0 : 6.0) * std::max(6.0, m_intervalFrames + 0.5 * m_jitterFrames);
      const double effOmega = (frames <= freeH)
          ? m_omega
          : m_omega * std::exp(-(frames - freeH) / dampScale);
      const double half = 0.5 * effOmega * frames;
      if (std::abs(half) >= 1e-4) {
        disp = Rotate2(v, half);
        const double scale = std::sin(half) / (0.5 * effOmega);
        disp[0] *= scale;
        disp[1] *= scale;
        const double len = Norm2(disp), lim = LimitXZ();
        if (len > lim) { disp[0] *= lim / len; disp[1] *= lim / len; }
        return disp;
      }
    }
    disp[0] *= frames;
    disp[1] *= frames;
    const double len = Norm2(disp), lim = LimitXZ();
    if (len > lim) { disp[0] *= lim / len; disp[1] *= lim / len; }
    return disp;
  }

  double LeadY(double vy, double frames) const {
    if (frames <= 0.0) return 0.0;
    const double freeY = std::min(5.0, m_intervalFrames + 2.0);
    double eff = frames;
    if (frames > freeY) {
      const double yH = std::clamp(1.5 * m_intervalFrames, 3.0, 6.0);
      eff = freeY + yH * (1.0 - std::exp(-(frames - freeY) / yH));
    }
    return std::clamp(vy * eff, -8.0, 8.0);
  }

  double LeadYaw16(double rate16, double frames) const {
    if (frames <= 0.0) return 0.0;
    const double dampScale = 2.5 * std::max(6.0, m_intervalFrames + 0.5 * m_jitterFrames);
    const double eff = dampScale * (1.0 - std::exp(-frames / dampScale));
    return std::clamp(rate16 * eff, -8192.0, 8192.0);
  }

  double LeadTilt16(double rate16, double frames) const {
    if (frames <= 0.0) return 0.0;
    const double h = std::clamp(m_intervalFrames, 2.0, 4.5);
    const double eff = h * (1.0 - std::exp(-frames / h));
    return std::clamp(rate16 * eff, -2048.0, 2048.0);
  }

  Vec3 BasePos(double elapsed) const {
    const double frames = EffectiveFrames(elapsed);
    const Vec2 xz = LeadXZ(m_velXZ, frames);
    const double dy = LeadY(m_velY, frames);
    return {m_pos[0] + xz[0], m_pos[1] + dy, m_pos[2] + xz[1]};
  }

  Vec3 EvaluatePosAtElapsed(double elapsed) const {
    const double e = std::exp(-elapsed / m_activeTau);
    const double vScale = SmoothMode() ? (2.5 * (1.0 - std::exp(-elapsed * 0.4)) * e) : 0.0;
    Vec3 p = BasePos(elapsed);
    for (unsigned i = 0; i < 3; ++i)
      p[i] += m_posCorr[i] * e + m_velCorr[i] * vScale;
    return p;
  }

  Vec3 EvaluateVel(uint64_t frame) const {
    const double elapsed = double(frame - m_receivedFrame);
    constexpr double eps = 1e-3;
    const Vec3 a = EvaluatePosAtElapsed(elapsed + eps);
    const Vec3 b = EvaluatePosAtElapsed(elapsed >= eps ? elapsed - eps : elapsed);
    const double inv = elapsed >= eps ? (0.5 / eps) : (1.0 / eps);
    return {(a[0] - b[0]) * inv, (a[1] - b[1]) * inv, (a[2] - b[2]) * inv};
  }

  Vec3 EvaluateAnglesAtElapsed(double elapsed) const {
    const double frames = EffectiveFrames(elapsed);
    const double e = std::exp(-elapsed / m_activeTau);
    const double vScale = SmoothMode() ? (2.5 * (1.0 - std::exp(-elapsed * 0.4)) * e) : 0.0;
    return {
      m_pitch16 + LeadTilt16(m_pitchRate16, frames) + m_angCorr[0] * e,
      m_yaw16 + LeadYaw16(m_yawRate16, frames) + m_angCorr[1] * e + m_yawVelCorr * vScale,
      m_roll16 + LeadTilt16(m_rollRate16, frames) + m_angCorr[2] * e
    };
  }

  double EvaluateYawVel(uint64_t frame) const {
    const double elapsed = double(frame - m_receivedFrame);
    constexpr double eps = 1e-3;
    const double a = EvaluateAnglesAtElapsed(elapsed + eps)[1];
    const double b = EvaluateAnglesAtElapsed(elapsed >= eps ? elapsed - eps : elapsed)[1];
    const double inv = elapsed >= eps ? (0.5 / eps) : (1.0 / eps);
    return WrapAngle16(a - b) * inv;
  }

  void Seed(const PoseSample& s, uint8_t seq7, uint64_t sourceUs,
            uint64_t localFrame, double ageFrames) {
    const double keepJitter = m_jitterFrames;
    const double keepLatency = m_latencyFrames;
    Reset();
    m_jitterFrames = keepJitter;
    m_latencyFrames = keepLatency;
    m_seeded = true;
    m_modelId = s.modelId;
    m_lastSeq7 = seq7;
    m_lastSourceUs = sourceUs;
    m_receivedFrame = localFrame;
    m_lastEnvelopeFrame = localFrame;
    m_lastEnvelopeSourceUs = sourceUs;
    m_lastEnvelopeSeq7 = seq7;
    m_age = std::clamp(ageFrames, 0.0, MaxFrames());
    m_pos = {s.x, s.y, s.z};
    m_lastNativeVelocity = {s.vx, s.vy, s.vz};
    m_pitch16 = double(s.pitch);
    m_yaw16 = double(s.yaw);
    m_roll16 = double(s.roll);
    const Vec2 natXZ{s.vx, s.vz};
    if (Norm2(natXZ) > 1e-4) {
      m_prevNativeXZ = natXZ;
      m_hasPrevNativeXZ = true;
    }
  }

public:
  void SetTickRate(unsigned hz) {
    hz = std::clamp(hz, 5u, 60u);
    m_intervalFrames = 60.0 / double(hz);
  }
  void Reset() {
    const double iv = m_intervalFrames;
    *this = PoseMotion();
    m_intervalFrames = iv;
  }

  bool Apply(const PoseSample& s, uint8_t seq7, uint64_t sourceUs,
             uint64_t localFrame, double sampleAgeFrames, double uncertaintyFrames,
             PoseOutput& out, bool nativeVelocityIsWorldSpace = true) {
    if (!std::isfinite(sampleAgeFrames) || !std::isfinite(uncertaintyFrames) ||
        sampleAgeFrames < 0.0 || uncertaintyFrames < 0.0) {
      Reset();
      return false;
    }
    if (!m_seeded || localFrame < m_receivedFrame || s.modelId != m_modelId) {
      Seed(s, seq7, sourceUs, localFrame, sampleAgeFrames);
      return false;
    }

    // Detect transport stalls from receive timing, not repeated native poses.
    const bool freshEnvelope = sourceUs != 0
        ? sourceUs != m_lastEnvelopeSourceUs : seq7 != m_lastEnvelopeSeq7;
    if (freshEnvelope) {
      m_lastEnvelopeFrame = localFrame;
      m_lastEnvelopeSourceUs = sourceUs;
      m_lastEnvelopeSeq7 = seq7;
    } else if (m_moving) {
      const double silence = double(localFrame - m_lastEnvelopeFrame);
      if (silence > m_intervalFrames + 2.0)
        m_jitterFrames = std::max(m_jitterFrames,
                                 std::min(24.0, silence - m_intervalFrames));
    }

    // Repeated snapshots continue from the last unique pose.
    const bool repeatedPose = s.x == m_pos[0] && s.y == m_pos[1] && s.z == m_pos[2] &&
        double(s.pitch) == m_pitch16 && double(s.yaw) == m_yaw16 && double(s.roll) == m_roll16 &&
        (!nativeVelocityIsWorldSpace ||
         (s.vx == m_lastNativeVelocity[0] && s.vy == m_lastNativeVelocity[1] && s.vz == m_lastNativeVelocity[2]));
    const uint32_t seqDelta = uint32_t((seq7 - m_lastSeq7) & 0x7Fu);
    double dt = 0.0;
    if (sourceUs != 0 && m_lastSourceUs != 0) {
      if (sourceUs < m_lastSourceUs) {
        Seed(s, seq7, sourceUs, localFrame, sampleAgeFrames);
        return false;
      }
      if (sourceUs > m_lastSourceUs && !repeatedPose) {
        if (seqDelta > 0u && seqDelta <= 60u) {
          dt = double(seqDelta);
        } else {
          const uint64_t deltaUs = sourceUs - m_lastSourceUs;
          if (deltaUs >= 8000ULL)
            dt = double(deltaUs) / 16667.0;
          else
            dt = std::max(1.0, double(localFrame - m_receivedFrame));
        }
      }
    } else {
      if (seqDelta > 0u && !repeatedPose) {
        if (seqDelta > 64u) {
          Seed(s, seq7, sourceUs, localFrame, sampleAgeFrames);
          return false;
        }
        dt = double(seqDelta);
      } else if (s.x != m_pos[0] || s.y != m_pos[1] || s.z != m_pos[2] ||
                 double(s.yaw) != m_yaw16) {
        const uint64_t gap = localFrame - m_receivedFrame;
        if (gap == 0) {
          Seed(s, seq7, sourceUs, localFrame, sampleAgeFrames);
          return false;
        }
        dt = double(gap);
      }
    }

    if (dt > 0.0) {
      const Vec2 chordXZ{(s.x - m_pos[0]) / dt, (s.z - m_pos[2]) / dt};
      const double chordY = (s.y - m_pos[1]) / dt;
      if (dt > 120.0 || Norm2(chordXZ) > kMaxSpeedXZ || std::abs(chordY) > kMaxSpeedY) {
        Seed(s, seq7, sourceUs, localFrame, sampleAgeFrames);
        return false;
      }

      // Save the displayed pose before changing smoothing state.
      const Vec3 oldPos = EvaluatePosAtElapsed(double(localFrame - m_receivedFrame));
      const Vec3 oldVel = m_moving ? EvaluateVel(localFrame)
                                   : Vec3{chordXZ[0], chordY, chordXZ[1]};
      const Vec3 oldAng = EvaluateAnglesAtElapsed(double(localFrame - m_receivedFrame));
      const double chordYawRate16 = WrapAngle16(double(s.yaw) - m_yaw16) / dt;
      const double oldYawVel = m_moving ? EvaluateYawVel(localFrame) : chordYawRate16;

      const double localGap = double(localFrame - m_receivedFrame);
      const double prevJitter = m_jitterFrames;
      const double arrJit = std::abs(localGap - dt);
      if (arrJit > m_jitterFrames)
        m_jitterFrames += 0.65 * (arrJit - m_jitterFrames);
      else
        m_jitterFrames *= 0.96;
      m_jitterFrames = std::min(m_jitterFrames, 24.0);

      const Vec2 expDispXZ = LeadXZ(m_velXZ, dt);
      const double errXZ = std::hypot(s.x - (m_pos[0] + expDispXZ[0]),
                                      s.z - (m_pos[2] + expDispXZ[1]));
      const double errY = std::abs(s.y - (m_pos[1] + m_velY * std::min(dt, 6.0)));
      if (m_moving && (errXZ > SnapXZ(dt) || errY > 40.0)) {
        m_jitterFrames = prevJitter;
        Seed(s, seq7, sourceUs, localFrame, sampleAgeFrames);
        return false;
      }

      const bool smooth = SmoothMode();
      const Vec2 natXZ{s.vx, s.vz};
      const Vec2 diffNat{natXZ[0] - chordXZ[0], natXZ[1] - chordXZ[1]};
      // Preserve real stops while ignoring legacy zero-filled velocity fields.
      const bool nativeStoppedXZ = Norm2(natXZ) < 1e-4 &&
          std::hypot(m_lastNativeVelocity[0], m_lastNativeVelocity[2]) > 1e-4;
      const bool trustNativeXZ = nativeVelocityIsWorldSpace && (nativeStoppedXZ ||
          (!(Norm2(natXZ) < 1e-4 && Norm2(chordXZ) > 0.08) &&
           Norm2(diffNat) <= std::max(0.12, 0.35 * Norm2(chordXZ) + 0.12 * dt)));

      double newOmega = 0.0;
      if (trustNativeXZ && m_hasPrevNativeXZ &&
          Norm2(m_prevNativeXZ) > 0.20 && Norm2(natXZ) > 0.20) {
        const double cross = m_prevNativeXZ[0] * natXZ[1] - m_prevNativeXZ[1] * natXZ[0];
        const double dot = m_prevNativeXZ[0] * natXZ[0] + m_prevNativeXZ[1] * natXZ[1];
        const double dtheta = std::atan2(cross, dot);
        if (std::abs(dtheta) < 1.2)
          newOmega = std::clamp(dtheta / std::max(1.0, dt), -0.055, 0.055);
        const double wBlend = dt <= 2.0 ? 0.35 : 0.65;
        m_omega = (1.0 - wBlend) * m_omega + wBlend * newOmega;
      } else if (smooth && m_moving && Norm2(m_chordXZ) > 0.20 && Norm2(chordXZ) > 0.20) {
        const double cross = m_chordXZ[0] * chordXZ[1] - m_chordXZ[1] * chordXZ[0];
        const double dot = m_chordXZ[0] * chordXZ[0] + m_chordXZ[1] * chordXZ[1];
        const double dtheta = std::atan2(cross, dot);
        if (std::abs(dtheta) < 1.2)
          newOmega = std::clamp(dtheta / std::max(1.0, 0.5 * (m_lastDt + dt)), -0.055, 0.055);
        const double wBlend = dt <= 2.0 ? 0.25 : 0.55;
        m_omega = (1.0 - wBlend) * m_omega + wBlend * newOmega;
      } else {
        m_omega = 0.0;
      }

      const Vec2 chordTangent = Rotate2(chordXZ, 0.5 * m_omega * dt);
      const Vec2 sampleVelXZ = trustNativeXZ ? natXZ : chordTangent;
      Vec2 nextVelXZ = sampleVelXZ;
      if (!trustNativeXZ && m_moving && (dt <= 2.0 || smooth)) {
        const double keep = dt <= 2.0 ? 0.45 : 0.18;
        nextVelXZ[0] = keep * m_velXZ[0] + (1.0 - keep) * sampleVelXZ[0];
        nextVelXZ[1] = keep * m_velXZ[1] + (1.0 - keep) * sampleVelXZ[1];
      }

      const bool nativeStoppedY = std::abs(s.vy) < 1e-4 && std::abs(m_lastNativeVelocity[1]) > 1e-4;
      const bool trustNativeY = nativeVelocityIsWorldSpace && (nativeStoppedY ||
          (!(std::abs(s.vy) < 1e-4 && std::abs(chordY) > 0.08) &&
           std::abs(s.vy - chordY) <= std::max(0.08, 0.35 * std::abs(chordY) + 0.08 * dt)));
      const double nextVelY = trustNativeY
                                  ? s.vy
                                  : (m_moving ? (0.25 * m_velY + 0.75 * chordY) : chordY);

      const double blendedYawRate =
          (m_moving && dt <= 4.0) ? (0.20 * m_yawRate16 + 0.80 * chordYawRate16) : chordYawRate16;
      const double nextYawRate16 = std::clamp(blendedYawRate, -2048.0, 2048.0);
      const double nextPitchRate16 =
          std::clamp(WrapAngle16(double(s.pitch) - m_pitch16) / dt, -768.0, 768.0);
      const double nextRollRate16 =
          std::clamp(WrapAngle16(double(s.roll) - m_roll16) / dt, -768.0, 768.0);

      m_chordXZ = chordXZ;
      m_lastDt = dt;
      if (trustNativeXZ) {
        m_prevNativeXZ = natXZ;
        m_hasPrevNativeXZ = true;
      } else {
        m_hasPrevNativeXZ = false;
      }

      if (uncertaintyFrames < 600.0 && sampleAgeFrames > 0.0) {
        if (sampleAgeFrames > m_latencyFrames)
          m_latencyFrames += 0.5 * (sampleAgeFrames - m_latencyFrames);
        else
          m_latencyFrames += 0.02 * (sampleAgeFrames - m_latencyFrames);
      }
      const double maxF = MaxFrames();
      const double clampedAge = std::min(sampleAgeFrames, maxF);
      const bool validClockAge = uncertaintyFrames < 600.0;
      double nextAge = validClockAge ? clampedAge : 0.0;
      if (m_moving) {
        if (smooth) {
          const double kinAge = std::clamp(m_age + (localGap - dt), 0.0, maxF);
          nextAge = (validClockAge && sampleAgeFrames > 0.0)
                        ? (kinAge + 0.20 * (clampedAge - kinAge))
                        : kinAge;
        } else if (validClockAge) {
          nextAge = m_age + 0.25 * (clampedAge - m_age);
        }
      }

      const bool wasMoving = m_moving;
      m_pos = {s.x, s.y, s.z};
      m_lastNativeVelocity = {s.vx, s.vy, s.vz};
      m_velXZ = nextVelXZ;
      m_velY = nextVelY;
      m_pitch16 = double(s.pitch);
      m_yaw16 = double(s.yaw);
      m_roll16 = double(s.roll);
      m_pitchRate16 = nextPitchRate16;
      m_yawRate16 = nextYawRate16;
      m_rollRate16 = nextRollRate16;
      m_lastSeq7 = seq7;
      m_lastSourceUs = sourceUs;
      m_receivedFrame = localFrame;
      m_age = nextAge;
      m_moving = true;

      m_posCorr = {};
      m_velCorr = {};
      m_angCorr = {};
      m_yawVelCorr = 0.0;

      const Vec3 newBasePos = BasePos(0.0);
      for (unsigned i = 0; i < 3; ++i)
        m_posCorr[i] = oldPos[i] - newBasePos[i];

      const Vec3 newBaseAng = EvaluateAnglesAtElapsed(0.0);
      for (unsigned i = 0; i < 3; ++i) {
        const double diff = WrapAngle16(oldAng[i] - newBaseAng[i]);
        m_angCorr[i] = (std::abs(diff) <= 16384.0) ? diff : 0.0;
      }

      m_activeTau = Tau();
      const double spdXZ = Norm2(m_velXZ);
      if (smooth && wasMoving && spdXZ > 0.22) {
        const double ahead = (m_posCorr[0] * m_velXZ[0] + m_posCorr[2] * m_velXZ[1]) / spdXZ;
        if (ahead > 0.0)
          m_activeTau = std::min(36.0, std::max(m_activeTau, ahead / (0.35 * spdXZ)));
      }
      if (smooth && wasMoving) {
        const Vec3 newBaseVel = EvaluateVel(localFrame);
        for (unsigned i = 0; i < 3; ++i)
          m_velCorr[i] = (oldVel[i] - newBaseVel[i]) * 0.85;
        const double newBaseYawVel = EvaluateYawVel(localFrame);
        m_yawVelCorr = WrapAngle16(oldYawVel - newBaseYawVel) * 0.85;
      }
    }

    if (!m_moving) return false;

    const double elapsed = double(localFrame - m_receivedFrame);
    Vec3 disp = EvaluatePosAtElapsed(elapsed);
    const Vec2 deltaXZ{disp[0] - m_pos[0], disp[2] - m_pos[2]};
    const double lenXZ = Norm2(deltaXZ), limXZ = LimitXZ();
    if (lenXZ > limXZ) {
      disp[0] = m_pos[0] + deltaXZ[0] * (limXZ / lenXZ);
      disp[2] = m_pos[2] + deltaXZ[1] * (limXZ / lenXZ);
    }
    disp[1] = m_pos[1] + std::clamp(disp[1] - m_pos[1], -12.0, 12.0);

    const Vec3 ang = EvaluateAnglesAtElapsed(elapsed);
    out.x = float(disp[0]);
    out.y = float(disp[1]);
    out.z = float(disp[2]);
    out.pitch = PackAngle16(ang[0]);
    out.yaw = PackAngle16(ang[1]);
    out.roll = PackAngle16(ang[2]);
    return true;
  }
};

struct Result {
  bool human = false;
  unsigned cpus = 0;
};

class Predictor {
  unsigned m_tickRateHz = 60;
  bool m_active = false;
  int32_t m_senderIndex = -1;
  uint32_t m_raceMode = 0;
  unsigned m_numCpuCars = 0;
  PoseMotion m_human;
  std::array<PoseMotion, MaxCpuCars> m_cpus;

  static void WritePose(uint8_t* out, size_t poseOffset, const PoseOutput& p) {
    Write16(out, poseOffset + 0x02, uint16_t(p.pitch));
    Write16(out, poseOffset + 0x04, uint16_t(p.yaw));
    Write16(out, poseOffset + 0x06, uint16_t(p.roll));
    WriteFloat(out, poseOffset + 0x08, p.x);
    WriteFloat(out, poseOffset + 0x0C, p.y);
    WriteFloat(out, poseOffset + 0x10, p.z);
  }

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
    m_raceMode = 0;
    m_numCpuCars = 0;
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
    if (!m_active || hdr.senderIndex != m_senderIndex || hdr.raceMode != m_raceMode ||
        hdr.numCpuCars != m_numCpuCars) {
      Reset();
      m_active = true;
      m_senderIndex = hdr.senderIndex;
      m_raceMode = hdr.raceMode;
      m_numCpuCars = hdr.numCpuCars;
    }

    PoseSample humanSample{};
    PoseOutput humanOut{};
    if (DecodePose(raw, HumanPoseOffset, humanSample)) {
      if (m_human.Apply(humanSample, hdr.seq7, sourceTimestampUs, localFrame,
                        sampleAgeFrames, uncertaintyFrames, humanOut)) {
        WritePose(out, HumanPoseOffset, humanOut);
        res.human = true;
      }
    } else {
      m_human.Reset();
    }

    for (unsigned i = 0; i < hdr.numCpuCars; ++i) {
      const size_t cpuOffset = CpuPoseBase + size_t(i) * PoseStride;
      PoseSample cpuSample{};
      PoseOutput cpuOut{};
      if (DecodePose(raw, cpuOffset, cpuSample)) {
        if (m_cpus[i].Apply(cpuSample, hdr.seq7, sourceTimestampUs, localFrame,
                            sampleAgeFrames, uncertaintyFrames, cpuOut, false)) {
          WritePose(out, cpuOffset, cpuOut);
          ++res.cpus;
        }
      } else {
        m_cpus[i].Reset();
      }
    }
    return res;
  }
};

// Preserve native nonparticipant exclusion using observed entry membership.
struct EntryRoster {
  uint32_t tag=0, seen=0, locked=0;
  bool tracking=false;
  uint64_t openedUs=0;
  void BeginFrame(const uint8_t* tx,unsigned local,uint64_t nowUs=0) {
    const uint32_t valid=Read32(tx,0x08);
    const bool entry=Read32(tx,0x00)==1 && IsChallengerSessionTag(valid);
    if(entry) {
      if(!tracking || valid!=tag) { tag=valid; seen=1u<<local; locked=0; openedUs=nowUs; }
      tracking=true;
    } else if(tracking) {
      // Native entry closes the race roster.
      locked=valid==1 ? seen : 0;
      tracking=false;
    }
    if(!entry && valid!=1) locked=0;
  }
  void Observe(const uint8_t* raw,unsigned cabinet,uint64_t receivedUs=UINT64_MAX) {
    if(!tracking || receivedUs<openedUs) return;
    const uint32_t valid=Read32(raw,0x08);
    if(Read32(raw,0x00)==1 && valid==tag) seen|=1u<<cabinet;
    if(Read32(raw,0x00)==0 || (valid!=tag && valid!=1)) seen&=~(1u<<cabinet);
  }
};
inline void ApplyChallengerEntryBridge(const uint8_t* localTx,const uint8_t* raw,
                                      size_t size,uint8_t* out,bool seen=false) {
  if(!localTx || !raw || !out || size<ActivePayloadSize || !seen) return;
  const uint32_t tag=Read32(localTx,0x08);
  if(Read32(localTx,0x00)!=1 || !IsChallengerSessionTag(tag)) return;
  const uint32_t phase=Read32(raw,0x00), valid=Read32(raw,0x08);
  const bool selecting=phase==1 && valid==tag;
  const bool finalized=(phase==1 || phase==2) && valid==1 && Read16(raw,0x88A)==tag;
  if(selecting || finalized) {
    // Clear stale title latches only for confirmed participants.
    Write32(out,0x860,0);
    if(finalized) Write32(out,0x08,tag);
  }
}

struct State {
  EntryRoster entry;
  unsigned tickRateHz = 60;
  std::vector<Predictor> predictors;
  std::array<uint32_t, MaxCabinets - 1> lastPhase = {~0u, ~0u, ~0u, ~0u, ~0u};
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

} // namespace Lemans24

#endif
