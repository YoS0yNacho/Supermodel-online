// Le Mans 24 (lemans24) packet presentation hooks; no socket I/O or worker RAM writes.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Supermodel.h"
#include "Model3/SimNetBoard.h"
#include "Prediction.h"

void CSimNetBoard::ConfigureLemans24Prediction()
{
  m_gameNet.lemans24.predictors.resize(m_numMachines - 1);
  m_gameNet.lemans24.SetTickRate(m_tickRateHz);
  InfoLog("Le Mans receive prediction r14-candidate: world-space velocity validation and first-gap coasting");
  InfoLog("%s 3D pose + angle prediction enabled (%u remote cabinets)",
          m_gameInfo.name.c_str(), unsigned(m_numMachines - 1));
}

void CSimNetBoard::ApplyLemans24Prediction()
{
  auto& state = m_gameNet.lemans24;
  const uint64_t now=uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count());
  state.entry.BeginFrame(CommRAM+0x100,m_asyncIndex[0],now);
  for (size_t slot = 0; slot < state.predictors.size(); ++slot)
  {
    const size_t offset = slot * m_segmentSize;
    const unsigned remoteCab =
        (unsigned(m_asyncIndex[0]) + unsigned(m_numMachines) - 1u - unsigned(slot)) %
        unsigned(m_numMachines);
    const bool validTiming = slot < m_asyncLatestAgeUs.size() &&
                             slot < m_asyncLatestUncertaintyUs.size() &&
                             m_asyncLatestUncertaintyUs[slot] < Lemans24::MaxTimingUncertaintyUs;
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
    if (m_asyncLatest.size() >= offset + m_segmentSize) {
      state.entry.Observe(src,remoteCab,slot<m_asyncWorkingReceiveUs.size()?m_asyncWorkingReceiveUs[slot]:0);
      Lemans24::ApplyChallengerEntryBridge(CommRAM + 0x100, src, m_segmentSize, dst,
        (state.entry.seen & (1u<<remoteCab))!=0);
    }

    std::vector<uint8_t> udpStorage;
    const uint8_t* presentation = PresentationSource(slot,udpStorage,sampleAgeFrames,uncertaintyFrames,&sourceTimestampUs);
    const auto result = state.predictors[slot].Apply(
        presentation, m_segmentSize, dst, m_gameNet.m_predictionFrame,
        sampleAgeFrames, uncertaintyFrames, sourceTimestampUs,
        remoteCab, unsigned(m_asyncIndex[0]), unsigned(m_numMachines));

    if (result.human && !state.humanLogged)
    {
      state.humanLogged = true;
      const uint32_t sub = Lemans24::Read32(src, 0x00);
      InfoLog("%s human prediction active: remote cabinet=%u raceSubstate=%u",
              m_gameInfo.name.c_str(), remoteCab, unsigned(sub));
    }
    if (result.cpus && !state.cpuLogged)
    {
      state.cpuLogged = true;
      InfoLog("%s CPU pose prediction active: %u remote AI records",
              m_gameInfo.name.c_str(), result.cpus);
    }
    if (slot < state.lastPhase.size() && m_asyncLatest.size() >= offset + m_segmentSize)
    {
      const uint32_t sub = Lemans24::Read32(src, 0x00);
      const uint32_t raceSub = Lemans24::Read32(src, 0x20);
      const uint32_t key = ((sub & 0xFFFFu) << 16) | (raceSub & 0xFFFFu);
      if (key != state.lastPhase[slot])
      {
        state.lastPhase[slot] = key;
        if (Lemans24::InRaceSubstate(sub))
          InfoLog("%s prediction slot=%u remote=%u phase=%u/%u%s", m_gameInfo.name.c_str(),
                  unsigned(slot), remoteCab, unsigned(sub), unsigned(raceSub),
                  result.human ? " (predicting)" : " (seeding remote pose)");
      }
    }
  }
}
