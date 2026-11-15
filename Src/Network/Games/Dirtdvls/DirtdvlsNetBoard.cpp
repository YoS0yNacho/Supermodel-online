// Dirt Devils (dirtdvls) packet presentation hooks; no socket I/O or worker RAM writes.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Supermodel.h"
#include "Model3/SimNetBoard.h"
#include "Prediction.h"

void CSimNetBoard::ConfigureDirtdvlsPrediction()
{
  m_gameNet.dirtdvls.predictors.resize(m_numMachines - 1);
  m_gameNet.dirtdvls.SetTickRate(m_tickRateHz);
  InfoLog("%s 3D pose + angle prediction enabled (%u remote cabinets, chord velocity)",
          m_gameInfo.name.c_str(), unsigned(m_numMachines - 1));
}

void CSimNetBoard::ApplyDirtdvlsPrediction()
{
  auto& state = m_gameNet.dirtdvls;
  for (size_t slot = 0; slot < state.predictors.size(); ++slot)
  {
    const size_t offset = slot * m_segmentSize;
    const unsigned remoteCab =
        (unsigned(m_asyncIndex[0]) + unsigned(m_numMachines) - 1u - unsigned(slot)) %
        unsigned(m_numMachines);
    const bool validTiming = slot < m_asyncLatestAgeUs.size() &&
                             slot < m_asyncLatestUncertaintyUs.size() &&
                             m_asyncLatestUncertaintyUs[slot] < Dirtdvls::MaxTimingUncertaintyUs;
    double sampleAgeFrames = validTiming
        ? double(m_asyncLatestAgeUs[slot]) / FrameDurationUs()
        : double(m_gameNet.m_asyncSnapshotAge);
    double uncertaintyFrames = slot < m_asyncLatestUncertaintyUs.size()
        ? double(m_asyncLatestUncertaintyUs[slot]) / FrameDurationUs()
        : 0.0;
    uint64_t sourceTimestampUs =
        slot < m_asyncLatestSourceUs.size() ? m_asyncLatestSourceUs[slot] : 0ULL;

    uint8_t* dst = CommRAM + 0x100 + m_segmentSize + offset;
    const uint8_t* src = m_asyncLatest.data() + offset;
    if (m_asyncLatest.size() < offset + m_segmentSize) continue;

    std::vector<uint8_t> udpStorage;
    const uint8_t* presentation = PresentationSource(slot,udpStorage,sampleAgeFrames,uncertaintyFrames,&sourceTimestampUs);
    const auto result = state.predictors[slot].Apply(
        presentation, m_segmentSize, dst, m_gameNet.m_predictionFrame,
        sampleAgeFrames, uncertaintyFrames, sourceTimestampUs,
        remoteCab, unsigned(m_asyncIndex[0]), unsigned(m_numMachines));

    if (result.human && !state.humanLogged)
    {
      state.humanLogged = true;
      InfoLog("%s human prediction active: remote cabinet=%u state=%08x",
              m_gameInfo.name.c_str(), remoteCab, unsigned(Dirtdvls::Read32(src, 0x00)));
    }
    if (result.cpus && !state.cpuLogged)
    {
      state.cpuLogged = true;
      InfoLog("%s CPU pose prediction active: %u remote AI records",
              m_gameInfo.name.c_str(), result.cpus);
    }
    if (slot < state.lastPhase.size())
    {
      const uint32_t key = Dirtdvls::Read32(src, 0x00);
      if (key != state.lastPhase[slot])
      {
        state.lastPhase[slot] = key;
        if (Dirtdvls::InRaceScene(key))
          InfoLog("%s prediction slot=%u remote=%u state=%08x%s", m_gameInfo.name.c_str(),
                  unsigned(slot), remoteCab, unsigned(key),
                  result.human ? " (predicting)" : " (seeding remote pose)");
      }
    }
  }
}
