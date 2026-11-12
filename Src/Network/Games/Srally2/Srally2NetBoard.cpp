// Sega Rally 2 (srally2) packet presentation hooks; no socket I/O or worker RAM writes.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Supermodel.h"
#include "Model3/SimNetBoard.h"
#include "Prediction.h"

void CSimNetBoard::ConfigureSrally2Prediction()
{
  m_gameNet.srally2.predictors.resize(m_numMachines - 1);
  m_gameNet.srally2.SetTickRate(m_tickRateHz);
  InfoLog("%s 3D pose + yaw/tilt prediction enabled (%u remote cabinets)",
          m_gameInfo.name.c_str(), unsigned(m_numMachines - 1));
}

void CSimNetBoard::ApplySrally2Prediction()
{
  auto& state = m_gameNet.srally2;
  for (size_t slot = 0; slot < state.predictors.size(); ++slot)
  {
    const size_t offset = slot * m_segmentSize;
    const unsigned remoteCab =
        (unsigned(m_asyncIndex[0]) + unsigned(m_numMachines) - 1u - unsigned(slot)) %
        unsigned(m_numMachines);
    const bool validTiming = slot < m_asyncLatestAgeUs.size() &&
                             slot < m_asyncLatestUncertaintyUs.size() &&
                             m_asyncLatestUncertaintyUs[slot] < Srally2::MaxTimingUncertaintyUs;
    double sampleAgeFrames = validTiming
        ? double(m_asyncLatestAgeUs[slot]) / FrameDurationUs()
        : double(m_gameNet.m_asyncSnapshotAge);
    double uncertaintyFrames = slot < m_asyncLatestUncertaintyUs.size()
        ? double(m_asyncLatestUncertaintyUs[slot]) / FrameDurationUs()
        : 0.0;
    uint64_t sourceTimestampUs =
        slot < m_asyncLatestSourceUs.size() ? m_asyncLatestSourceUs[slot] : 0ULL;

    std::vector<uint8_t> udpStorage;
    const uint8_t* presentation = PresentationSource(slot,udpStorage,sampleAgeFrames,uncertaintyFrames,&sourceTimestampUs);
    const auto result = state.predictors[slot].Apply(
        presentation, m_segmentSize,
        CommRAM + 0x100 + m_segmentSize + offset, m_gameNet.m_predictionFrame,
        sampleAgeFrames, uncertaintyFrames, sourceTimestampUs,
        remoteCab, unsigned(m_asyncIndex[0]), unsigned(m_numMachines));

    if (result.human && !state.humanLogged)
    {
      state.humanLogged = true;
      const uint32_t phase = Srally2::Read32(m_asyncLatest.data() + offset, 0x18);
      InfoLog("%s human prediction active: remote cabinet=%u linkPhase=%u",
              m_gameInfo.name.c_str(), remoteCab, unsigned(phase));
    }
    if (result.cpus && !state.cpuLogged)
    {
      state.cpuLogged = true;
      InfoLog("%s CPU pose prediction active: %u remote CPU records",
              m_gameInfo.name.c_str(), result.cpus);
    }
    if (m_asyncLatest.size() >= offset + m_segmentSize)
    {
      const uint32_t mode = Srally2::Read32(m_asyncLatest.data() + offset, 0x08);
      const uint32_t phase = Srally2::Read32(m_asyncLatest.data() + offset, 0x18);
      const uint32_t key = (mode << 16) | (phase & 0xFFFFu);
      if (key != state.lastPhase)
      {
        state.lastPhase = key;
        if (mode == 1u && Srally2::InRacePhase(phase))
          InfoLog("%s prediction phase=%u%s", m_gameInfo.name.c_str(), unsigned(phase),
                  result.human ? " (predicting)" : " (seeding remote pose)");
      }
    }
  }
}
