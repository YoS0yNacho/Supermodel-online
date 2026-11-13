/*
 * Supermodel — experimental dayto2pe receive-side position prediction.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Protocol evidence and limitations: Docs/Daytona2Prediction.md.
 * This is NOT a generic float scanner. Only the primary human-cabinet X/Z
 * fields are writable through Predictor. Daytona2CpuPrediction.h separately
 * validates native CPU ownership before reusing MotionPredictor for CPU X/Z.
 */
#ifndef INCLUDED_DAYTONA2PREDICTION_H
#define INCLUDED_DAYTONA2PREDICTION_H

#include "Policy.h"
#include "../CorrectionSmoothing.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace Dayto2pe
{
constexpr size_t PacketSize = 0x350;
constexpr size_t PositionX = 0x6c;
constexpr size_t PositionZ = 0x70;

// PowerPC fields are big-endian; HLE packet words swap 16-bit halves.
inline uint16_t Read16(const uint8_t* p, size_t offset)
{
  return uint16_t(uint16_t(p[offset ^ 2]) << 8 | p[(offset + 1) ^ 2]);
}
inline uint32_t Read32(const uint8_t* p, size_t offset)
{
  return uint32_t(Read16(p, offset)) << 16 | Read16(p, offset + 2);
}
inline float ReadFloat(const uint8_t* p, size_t offset)
{
  static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
                "dayto2pe positions require IEEE-754 binary32");
  const uint32_t bits = Read32(p, offset);
  float value;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}
inline void WriteFloat(uint8_t* p, size_t offset, float value)
{
  uint32_t bits;
  std::memcpy(&bits, &value, sizeof(bits));
  for (unsigned i = 0; i < 4; i++)
    p[(offset + i) ^ 2] = uint8_t(bits >> (24 - i * 8));
}

struct Position
{
  double x = 0;
  double z = 0;
};
inline Position operator+(Position a, Position b) { return {a.x + b.x, a.z + b.z}; }
inline Position operator-(Position a, Position b) { return {a.x - b.x, a.z - b.z}; }
inline Position operator*(Position a, double b) { return {a.x * b, a.z * b}; }
inline double Length(Position p) { return std::hypot(p.x, p.z); }

struct Sample
{
  uint16_t tick = 0;
  Position position;
};

inline bool Decode(const uint8_t* packet, size_t size, Sample& sample, const Profile& profile = PowerEdition)
{
  if (!packet || size != PacketSize)
    return false;
  // Header masks and required receiver flags differ by edition.
  if (((Read32(packet, 0x04) >> profile.stateShift) & 31) != profile.raceState ||
      (Read32(packet, profile.classOffset) & profile.classMask) != profile.classValue ||
      (Read32(packet, 0x64) & 0x81) != 0x81)
    return false;
  uint32_t checksum = 0;
  for (size_t offset = 4; offset < 0x64; offset += 4)
    if (offset != 0x3c)
      checksum += Read32(packet, offset);
  if (checksum != Read32(packet, 0x3c))
    return false;
  sample.tick = Read16(packet, 0x3a);
  sample.position = {ReadFloat(packet, PositionX), ReadFloat(packet, PositionZ)};
  return std::isfinite(sample.position.x) && std::isfinite(sample.position.z) &&
         std::abs(sample.position.x) <= 10000 && std::abs(sample.position.z) <= 10000;
}

template<unsigned Horizon>
class MotionPredictor
{
public:
  // Prediction horizons use emulated frames and are bounded by age and distance.
  static constexpr unsigned MaxLeadFrames = Horizon;
  static constexpr double MinAdaptiveBufferFrames = 1.0;
  static constexpr double MaxAdaptiveBufferFrames = 6.0;
  static constexpr double MaxLatencyFrames = 24.0;
  static constexpr double MaxLatencyBufferFrames = 16.0;
  static constexpr unsigned MaxSourceGap = 120;
  static constexpr double MaxSpeed = 4.0;       // world units / source frame
  static constexpr double MaxLeadDistance = 16.0;
  static constexpr double SnapDistance = 12.0;
  static constexpr double CorrectionFrames = 6.0;
  static constexpr double HoldHeadroomFrames = 3.0;

  // Shorten projection during turns and major speed changes.
  static double LeadConfidence(Position previous, Position current)
  {
    const double before = Length(previous), now = Length(current);
    constexpr double epsilon = 0.05;
    if (before < epsilon || now < epsilon)
      return before < epsilon && now < epsilon ? 1.0 : 0.25;
    const double cosine = (previous.x * current.x + previous.z * current.z) / (before * now);
    const double direction = std::clamp((cosine - 0.25) / 0.75, 0.0, 1.0);
    const double speedRatio = std::min(before, now) / std::max(before, now);
    const double speed = std::clamp((speedRatio - 0.25) / 0.75, 0.0, 1.0);
    return std::min(direction, speed);
  }

  void SetTickRate(unsigned tickRateHz)
  {
    tickRateHz = std::clamp(tickRateHz, 5u, 60u);
    m_intervalFrames = 60.0 / double(tickRateHz);
  }
  void Reset()
  {
    const double interval = m_intervalFrames;
    *this = MotionPredictor();
    m_intervalFrames = interval;
  }
  double AdaptiveBufferFrames() const { return m_bufferFrames; }
  double FilteredAgeFrames() const { return m_age; }
  double LatencyFrames() const { return m_latencyFrames; }

  // Prediction writes to output only; source ticks may wrap.
  bool Apply(const uint8_t* raw, size_t size, uint8_t* output,
             uint64_t localFrame, double sampleAgeFrames, double timingUncertaintyFrames = 0.0,
             const Profile& profile = PowerEdition)
  {
    Sample sample;
    if (!output || !Decode(raw, size, sample, profile))
    {
      Reset();
      return false;
    }
    Position displayed;
    if (!ApplySample(sample, displayed, localFrame, sampleAgeFrames, timingUncertaintyFrames)) return false;
    WriteFloat(output, PositionX, float(displayed.x));
    WriteFloat(output, PositionZ, float(displayed.z));
    return true;
  }

  bool ApplySample(const Sample& sample, Position& displayed, uint64_t localFrame,
                   double sampleAgeFrames, double timingUncertaintyFrames = 0.0)
  {
    if (!std::isfinite(sample.position.x) || !std::isfinite(sample.position.z) ||
        std::abs(sample.position.x) > 10000 || std::abs(sample.position.z) > 10000 ||
        !std::isfinite(sampleAgeFrames) || !std::isfinite(timingUncertaintyFrames) ||
        sampleAgeFrames < 0 || timingUncertaintyFrames < 0)
    { Reset(); return false; }
    if (!m_initialized || localFrame < m_receivedFrame)
    {
      Seed(sample, localFrame, sampleAgeFrames, timingUncertaintyFrames);
      return false;
    }
    const unsigned delta = uint16_t(sample.tick - m_sample.tick);
    if (delta == 0)
    {
      // Reject pose changes without a new source tick.
      if (sample.position.x != m_sample.position.x || sample.position.z != m_sample.position.z)
      {
        Seed(sample, localFrame, sampleAgeFrames, timingUncertaintyFrames);
        return false;
      }
    }
    else
    {
      const Position rawVelocity = (sample.position - m_sample.position) * (1.0 / delta);
      if (delta > MaxSourceGap || Length(rawVelocity) > MaxSpeed)
      {
        Seed(sample, localFrame, sampleAgeFrames, timingUncertaintyFrames);
        return false;
      }
      // Save the current pose before changing smoothing state.
      const Position oldDisplay = Evaluate(localFrame);
      const Position oldVel = m_hasVelocity ? EvaluateVelocity(localFrame) : rawVelocity;

      const double localGap = double(localFrame - m_receivedFrame);
      const double prevJitter = m_jitterFrames;
      const double prevUncert = m_lastUncertainty;
      const double arrivalJitter = std::abs(localGap - double(delta));
      if (arrivalJitter > m_jitterFrames)
        m_jitterFrames += 0.65 * (arrivalJitter - m_jitterFrames);
      else
        m_jitterFrames *= 0.96;
      m_jitterFrames = std::min(m_jitterFrames, 24.0);
      m_lastUncertainty = timingUncertaintyFrames;

      const bool smoothMode = IsSmoothMode();
      // Check source motion before blending estimated velocity.
      const Position expectedPos = smoothMode
          ? (m_sample.position + ArcLead(m_velocity, m_omega, double(delta)))
          : (m_sample.position + m_velocity * double(delta));
      if (m_hasVelocity && Length(sample.position - expectedPos) > EffectiveSnapDistance(delta))
      {
        m_jitterFrames = prevJitter;
        m_lastUncertainty = prevUncert;
        Seed(sample, localFrame, sampleAgeFrames, timingUncertaintyFrames);
        return false;
      }

      double newOmega = 0.0;
      if (smoothMode && m_hasVelocity)
      {
        const double prevSpeed = Length(m_chordVelocity), curSpeed = Length(rawVelocity);
        if (prevSpeed > 0.15 && curSpeed > 0.15)
        {
          const double cross = m_chordVelocity.x * rawVelocity.z - m_chordVelocity.z * rawVelocity.x;
          const double dot = m_chordVelocity.x * rawVelocity.x + m_chordVelocity.z * rawVelocity.z;
          const double dtheta = std::atan2(cross, dot);
          if (std::abs(dtheta) < 1.0)
          {
            const double span = std::max(1.0, 0.5 * double(m_lastDelta + delta));
            newOmega = std::clamp(dtheta / span, -0.045, 0.045);
          }
        }
        const double omegaBlend = delta <= 2 ? 0.20 : 0.55;
        m_omega = (1.0 - omegaBlend) * m_omega + omegaBlend * newOmega;
      }
      else
      {
        m_omega = 0.0;
      }

      const Position prevForConfidence = smoothMode ? m_chordVelocity : m_velocity;
      const double rawConfidence = m_hasVelocity ? LeadConfidence(prevForConfidence, rawVelocity) : 1.0;
      const Position sampleTangent = smoothMode
          ? Rotate(rawVelocity, 0.5 * m_omega * double(delta))
          : rawVelocity;
      const double keep = (m_hasVelocity && delta <= 2) ? (m_jitterFrames > 0.75 ? 0.50 : 0.50 * rawConfidence)
          : ((smoothMode && m_hasVelocity) ? 0.15 * rawConfidence : 0.0);
      const Position velocity = m_velocity * keep + sampleTangent * (1.0 - keep);
      m_chordVelocity = rawVelocity;
      m_lastDelta = delta;

      m_leadConfidence = (!m_hasVelocity || rawConfidence == 0.0) ? rawConfidence :
          m_leadConfidence + (rawConfidence < m_leadConfidence ? 0.50 : 0.25) * (rawConfidence - m_leadConfidence);
      const double maxHorizon = EffectiveMaxLeadFrames();
      const bool validClockAge = timingUncertaintyFrames < 600.0;
      if (!m_hasVelocity)
      {
        m_age = validClockAge ? std::clamp(sampleAgeFrames, 0.0, maxHorizon) : 0.0;
      }
      else if (smoothMode)
      {
        // Estimate queue delay from local/source frame deltas.
        const double kinematicAge = std::clamp(m_age + (localGap - double(delta)), 0.0, maxHorizon);
        if (validClockAge)
        {
          const double measuredAge = std::clamp(sampleAgeFrames, 0.0, maxHorizon);
          m_age = kinematicAge + 0.20 * (measuredAge - kinematicAge);
        }
        else
        {
          m_age = kinematicAge;
        }
      }
      else
      {
        const double measuredAge = std::clamp(sampleAgeFrames, 0.0, maxHorizon);
        m_age = m_age + 0.25 * (measuredAge - m_age);
      }
      if (validClockAge)
      {
        // Track sustained one-way age separately from arrival jitter.
        if (sampleAgeFrames > m_latencyFrames)
          m_latencyFrames += 0.5 * (sampleAgeFrames - m_latencyFrames);
        else
          m_latencyFrames += 0.02 * (sampleAgeFrames - m_latencyFrames);
        UpdateAdaptiveBuffer(sampleAgeFrames, timingUncertaintyFrames);
      }
      const bool hadVelocity = m_hasVelocity;
      m_sample = sample;
      m_receivedFrame = localFrame;
      m_velocity = velocity;
      m_hasVelocity = true;
      m_correction = {0.0, 0.0};
      m_velCorrection = {0.0, 0.0};
      const Position newTarget = BaseTarget(0.0);
      // Blend corrections to avoid jumps on arrival.
      m_correction = oldDisplay - newTarget;
      m_activeTau = EffectiveCorrectionFrames();
      const double spd = Length(m_velocity);
      // Small corrections blend slowly (no visible nudge); large ones keep the fast blend.
      m_activeTau = GameNetwork::ErrorScaledTau(m_activeTau, Length(m_correction), spd, SnapDistance / 24.0, m_latencyFrames);
      if (smoothMode && hadVelocity && spd > 0.2)
      {
        const double ahead = (m_correction.x * m_velocity.x + m_correction.z * m_velocity.z) / spd;
        if (ahead > 0.0)
          m_activeTau = std::min(36.0, std::max(m_activeTau, ahead / (0.35 * spd)));
      }
      if (smoothMode && hadVelocity)
      {
        const Position newBaseVel = EvaluateVelocity(localFrame);
        const double dampGain = (m_jitterFrames > 0.75) ? 0.85 : 0.65;
        m_velCorrection = (oldVel - newBaseVel) * dampGain;
        // A velocity mismatch can never legitimately exceed the speed cap.
        const double velCorrectionLength = Length(m_velCorrection);
        if (velCorrectionLength > MaxSpeed)
          m_velCorrection = m_velCorrection * (MaxSpeed / velCorrectionLength);
      }
    }
    if (!m_hasVelocity)
      return false;
    displayed = Evaluate(localFrame);
    return true;
  }

private:
  bool IsSmoothMode() const
  {
    return m_intervalFrames > 1.001 || m_jitterFrames > 0.75;
  }
  double EffectiveMaxLeadFrames(double liveStallFrames = 0.0) const
  {
    // The horizon must leave room above the latency for holding through a
    // late packet: the lead is about `latency` frames when a sample arrives
    // and grows one frame per frame without one. Capped at exactly the
    // latency (as before, kept at smoothing level 0) a one-frame-late packet
    // clipped the hold and the car visibly stalled for a frame - the
    // "micro rubber band" of 150-250 ms relays, where the fixed 8-frame floor
    // no longer provided that room.
    const double headroom = GameNetwork::SmoothingLevel() ? HoldHeadroomFrames : 0.0;
    const double latencyHorizon = std::min(m_latencyFrames + headroom, MaxLatencyFrames);
    if (!IsSmoothMode() && liveStallFrames <= 0.0)
      return std::max(double(MaxLeadFrames), latencyHorizon);
    const double jitterBoost = (m_jitterFrames > 0.75 || liveStallFrames > 0.0)
        ? (1.8 * std::max(m_jitterFrames, liveStallFrames) + 10.0) : 0.0;
    // Low tick rates extend the lead horizon by measured sample age.
    return std::max(std::min(42.0, double(MaxLeadFrames) + 1.8 * (m_intervalFrames - 1.0) + jitterBoost),
                    latencyHorizon);
  }
  double EffectiveMaxAdaptiveBuffer() const
  {
    const double base = m_intervalFrames <= 1.001 ? MaxAdaptiveBufferFrames
        : std::min(12.0, MaxAdaptiveBufferFrames + 0.5 * (m_intervalFrames - 1.0));
    return std::max(base, std::min(m_latencyFrames, MaxLatencyBufferFrames));
  }
  double EffectiveMaxLeadDistance(double liveStallFrames = 0.0) const
  {
    if (!IsSmoothMode() && liveStallFrames <= 0.0)
      return m_latencyFrames > double(MaxLeadFrames)
          ? std::max(MaxLeadDistance,
                     std::min(m_latencyFrames, MaxLatencyFrames) * 3.6)
          : MaxLeadDistance;
    return std::max(MaxLeadDistance, EffectiveMaxLeadFrames(liveStallFrames) * 3.6);
  }
  double EffectiveSnapDistance(unsigned delta = 1) const
  {
    if (!IsSmoothMode())
      return SnapDistance;
    return SnapDistance + 2.5 * (m_intervalFrames - 1.0) +
        (m_jitterFrames > 0.75 ? 1.6 * double(std::min(delta, 45u)) : 0.0);
  }
  double EffectiveCorrectionFrames() const
  {
    // Increase smoothing for high one-way latency (from 8 frames; 18 at level 0).
    const double farTau = GameNetwork::FarLatencyTau(m_latencyFrames);
    return std::max(CorrectionFrames, m_intervalFrames * 1.25 +
        (m_jitterFrames > 0.75 ? 0.35 * m_jitterFrames : 0.0) + farTau);
  }
  static Position Rotate(Position v, double angle)
  {
    const double c = std::cos(angle), s = std::sin(angle);
    return {v.x * c - v.z * s, v.x * s + v.z * c};
  }
  static double SoftCap(double x, double cap, double knee = 2.0)
  {
    if (x <= 0.0) return 0.0;
    const double threshold = std::max(0.0, cap - knee);
    if (x <= threshold || knee <= 1e-6)
      return std::min(x, cap);
    return cap - knee * std::exp(-(x - threshold) / knee);
  }
  void Seed(const Sample& sample, uint64_t frame, double age, double uncert = 0.0)
  {
    const double keepJitter = m_jitterFrames;
    const double keepLatency = m_latencyFrames;
    Reset();
    m_jitterFrames = keepJitter;
    m_latencyFrames = keepLatency;
    m_lastUncertainty = uncert;
    m_initialized = true;
    m_sample = sample;
    m_receivedFrame = frame;
    m_age = (uncert < 600.0) ? std::clamp(age, 0.0, EffectiveMaxLeadFrames()) : 0.0;
  }
  void UpdateAdaptiveBuffer(double age, double uncertainty)
  {
    if (!std::isfinite(age) || !std::isfinite(uncertainty) || age < 0 || uncertainty < 0)
      return;
    const double target = std::clamp(age + 0.25 * uncertainty,
        MinAdaptiveBufferFrames, EffectiveMaxAdaptiveBuffer());
    const double smoothed = m_bufferFrames + 0.20 * (target - m_bufferFrames);
    m_bufferFrames += std::clamp(smoothed - m_bufferFrames, -1.0, 1.0);
  }
  static Position Lead(Position velocity, double frames, double maxDist = MaxLeadDistance)
  {
    Position lead = velocity * frames;
    const double distance = Length(lead);
    if (distance > maxDist)
      lead = lead * (maxDist / distance);
    return lead;
  }
  Position ArcLead(Position velocity, double omega, double frames, double liveStallFrames = 0.0) const
  {
    if (frames <= 0.0) return {0.0, 0.0};
    // Preserve curved motion; damp only beyond the confidence horizon.
    const bool farRelay = m_latencyFrames > 12.0;
    const double freeH = farRelay
        ? std::min(24.0, m_intervalFrames + 6.0 + (m_latencyFrames - 12.0) * 0.5)
        : std::min(16.0, m_intervalFrames + 6.0);
    const double dampScale = (farRelay ? 8.0 : 6.0)
        * std::max(6.0, m_intervalFrames + 0.5 * m_jitterFrames);
    const double effOmega = (frames <= freeH) ? omega
        : omega * std::exp(-(frames - freeH) / dampScale);
    const double halfTheta = 0.5 * effOmega * frames;
    Position disp = (std::abs(halfTheta) < 1e-4)
        ? (velocity * frames)
        : (Rotate(velocity, halfTheta) * (std::sin(halfTheta) / (0.5 * effOmega)));
    const double distance = Length(disp);
    const double maxDist = EffectiveMaxLeadDistance(liveStallFrames);
    if (distance > maxDist)
      disp = disp * (maxDist / distance);
    return disp;
  }
  Position BaseTarget(double elapsed) const
  {
    // Detect packet droughts using receive timing.
    const double liveStall = (m_lastUncertainty > 0.0 && elapsed > m_intervalFrames + 1.0)
        ? std::min(18.0, elapsed - (m_intervalFrames + 1.0)) : 0.0;
    if (!IsSmoothMode() && liveStall <= 0.0)
    {
      const double sampleLead = std::min(m_bufferFrames, m_age) * m_leadConfidence;
      const double holdLead = m_leadConfidence > 0.0 ? elapsed : 0.0;
      const double leadFrames = std::min<double>(EffectiveMaxLeadFrames(),
                                                 sampleLead + holdLead);
      return m_sample.position + Lead(m_velocity, leadFrames,
                                      EffectiveMaxLeadDistance());
    }
    // Keep the correction curve continuous when drought state changes.
    const double normalLead = std::min(m_bufferFrames, m_age) * m_leadConfidence;
    // The non-smooth form must equal the branch above at liveStall == 0:
    // EvaluateVelocity differentiates across this point, and a step here
    // used to become a huge velocity correction on a one-frame-late packet.
    const Position normalTarget = IsSmoothMode()
        ? m_sample.position + ArcLead(m_velocity, m_omega,
            SoftCap(normalLead + elapsed, EffectiveMaxLeadFrames(), 2.0))
        : m_sample.position + Lead(m_velocity,
            std::min<double>(EffectiveMaxLeadFrames(), normalLead + (m_leadConfidence > 0.0 ? elapsed : 0.0)),
            EffectiveMaxLeadDistance());
    const double stallBlend = m_jitterFrames > 0.75 ? 1.0
        : std::clamp(liveStall / 6.0, 0.0, 1.0);
    if (stallBlend <= 0.0) return normalTarget;
    const double confScale = 0.85 + 0.15 * m_leadConfidence;
    const double sampleLead = std::min(m_bufferFrames, m_age) * confScale +
        std::max(0.0, m_age - m_bufferFrames);
    const double maxLead = EffectiveMaxLeadFrames(liveStall);
    const double knee = std::min(8.0, 0.25 * maxLead);
    const double leadFrames = SoftCap(sampleLead + elapsed, maxLead, knee);
    const Position droughtTarget = m_sample.position +
        ArcLead(m_velocity, m_omega, leadFrames, liveStall);
    return normalTarget + (droughtTarget - normalTarget) * stallBlend;
  }
  Position EvaluateAtElapsed(double elapsed) const
  {
    const double tau = m_activeTau;
    const double e = std::exp(-elapsed / tau);
    Position pos = BaseTarget(elapsed) + m_correction * e;
    if (IsSmoothMode())
    {
      const double vRate = (m_jitterFrames > 0.75) ? 0.40 : 0.50;
      const double vAmp = 1.0 / vRate;
      pos = pos + m_velCorrection * (vAmp * (1.0 - std::exp(-elapsed * vRate))) * e;
    }
    return pos;
  }
  Position EvaluateVelocity(uint64_t frame) const
  {
    const double elapsed = double(frame - m_receivedFrame);
    constexpr double eps = 1e-3;
    if (elapsed >= eps)
      return (EvaluateAtElapsed(elapsed + eps) - EvaluateAtElapsed(elapsed - eps)) * (0.5 / eps);
    return (EvaluateAtElapsed(elapsed + eps) - EvaluateAtElapsed(elapsed)) * (1.0 / eps);
  }
  Position Evaluate(uint64_t frame) const
  {
    return EvaluateAtElapsed(double(frame - m_receivedFrame));
  }

  double m_intervalFrames = 1.0;
  double m_jitterFrames = 0.0;
  double m_lastUncertainty = 0.0;
  double m_activeTau = CorrectionFrames;
  bool m_initialized = false;
  bool m_hasVelocity = false;
  Sample m_sample;
  unsigned m_lastDelta = 1;
  Position m_chordVelocity;
  Position m_velocity;
  double m_omega = 0.0;
  Position m_correction;
  Position m_velCorrection;
  uint64_t m_receivedFrame = 0;
  double m_age = 0;
  double m_latencyFrames = 0.0;
  double m_bufferFrames = MinAdaptiveBufferFrames;
  double m_leadConfidence = 1.0;
};
using Predictor = MotionPredictor<8>;
}
#endif
