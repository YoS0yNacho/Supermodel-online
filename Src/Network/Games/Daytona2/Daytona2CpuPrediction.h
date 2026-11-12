// Verified per-edition Daytona compact CPU-car receive-copy prediction. GPL-3.0-or-later.
#ifndef INCLUDED_DAYTONA2CPUPREDICTION_H
#define INCLUDED_DAYTONA2CPUPREDICTION_H
#include "Daytona2Prediction.h"
#include "Policy.h"
#include <array>

namespace Dayto2pe
{
// Snapshot native ownership tables between frames; reject ambiguous routes.
struct CpuRouting
{
  static constexpr unsigned MaxOwners = 18, MaxCars = 40;
  struct Owner
  {
    uint8_t format = 0, count = 0;
    std::array<uint8_t, MaxCars> ids{};
  };
  uint8_t local = 0, machines = 0;
  std::array<Owner, MaxOwners> owners{};
  std::array<uint8_t, MaxCars> types{}, authorities{};
};

class CpuPredictor
{
public:
  static constexpr unsigned MaxLeadFrames = 8; // about 133 ms at nominal 60 Hz; conservative for stale CPU samples
  void SetTickRate(unsigned tickRateHz)
  {
    m_tickRateHz = std::clamp(tickRateHz, 5u, 60u);
    for (auto& m : m_motion)
      m.SetTickRate(m_tickRateHz);
  }
  void Reset()
  {
    const unsigned hz = m_tickRateHz;
    *this = CpuPredictor();
    SetTickRate(hz);
  }

  unsigned Apply(const uint8_t* raw, size_t size, uint8_t* output,
                 uint64_t frame, double age, double uncertainty, const CpuRouting& routing,
                 unsigned owner, const Profile& profile = PowerEdition)
  {
    Sample sampleHeader;
    if (!output || !Decode(raw, size, sampleHeader, profile) || routing.machines < 2 ||
        routing.machines >= CpuRouting::MaxOwners || owner == 0 || owner > routing.machines ||
        routing.local == 0 || routing.local > routing.machines || owner == routing.local)
    { Reset(); return 0; }
    const auto& map = routing.owners[owner];
    const auto byte = [raw](size_t o) { return raw[o ^ 2]; };
    const unsigned header = profile.compactHeader;
    const unsigned format = byte(header), count = byte(header + 2);
    const unsigned stride = format == 2 ? 16 : 24;
    if ((format != 2 && format != 3) || !(byte(header + 3) & 1) ||
        format != map.format || count != map.count || count > CpuRouting::MaxCars ||
        header + 4 + count * stride > size)
    { Reset(); return 0; }
    // Reject ambiguous routing before prediction writes.
    std::array<bool, CpuRouting::MaxCars> seen{};
    for (unsigned i = 0; i < count; ++i)
    {
      const unsigned id = map.ids[i];
      if (id >= CpuRouting::MaxCars || seen[id]) { Reset(); return 0; }
      seen[id] = true;
    }
    if (m_owner != owner || m_format != format || m_count != count || m_header != header)
    {
      Reset(); m_header = header; m_owner = owner; m_format = format; m_count = count;
      m_ids.fill(0xff);
    }
    unsigned modified = 0;
    for (unsigned i = 0; i < count; ++i)
    {
      const unsigned id = map.ids[i];
      auto& prediction = m_motion[i];
      if (m_ids[i] != id) { prediction.Reset(); m_ids[i] = uint8_t(id); }
      // Type 1 is human; type 2 is distributed CPU authority.
      if (routing.types[id] != 2 || routing.authorities[id] != owner)
      { prediction.Reset(); continue; }
      const size_t offset = header + 4 + i * stride;
      const Sample sample{sampleHeader.tick, {ReadFloat(raw, offset), ReadFloat(raw, offset + 4)}};
      Position display;
      if (prediction.ApplySample(sample, display, frame, age, uncertainty))
      {
        WriteFloat(output, offset, float(display.x));
        WriteFloat(output, offset + 4, float(display.z));
        ++modified;
      }
    }
    return modified;
  }

  unsigned Apply(const uint8_t* raw, size_t size, uint8_t* output,
                 uint64_t frame, unsigned age, const CpuRouting& routing,
                 unsigned owner, const Profile& profile = PowerEdition)
  {
    return Apply(raw, size, output, frame, double(age), 0.0, routing, owner, profile);
  }
private:
  unsigned m_tickRateHz = 60;
  unsigned m_header = 0;
  unsigned m_owner = CpuRouting::MaxOwners, m_format = 0, m_count = 0;
  std::array<uint8_t, CpuRouting::MaxCars> m_ids{};
  std::array<MotionPredictor<MaxLeadFrames>, CpuRouting::MaxCars> m_motion{};
};
}
#endif
