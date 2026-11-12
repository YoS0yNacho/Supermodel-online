// Scud packet presentation hooks; no socket I/O or worker RAM writes. GPL-3.0-or-later.
#include "Supermodel.h"
#include "Model3/SimNetBoard.h"
#include "Prediction.h"
void CSimNetBoard::ConfigureScudPrediction()
{
  m_gameNet.scud.predictors.resize(m_numMachines-1);
  m_gameNet.scud.SetTickRate(m_tickRateHz);
  InfoLog("%s human X/Z and native CPU-progress prediction enabled (%u remote cabinets)",m_gameInfo.name.c_str(),unsigned(m_numMachines-1));
}
void CSimNetBoard::ApplyScudPrediction()
{
  auto& state=m_gameNet.scud;
  const bool matched=state.routing.valid && state.routing.local==m_asyncIndex[0]+1 && state.routing.machines==m_numMachines;
  for(size_t slot=0;slot<state.predictors.size();++slot)
  {
    const size_t offset=slot*m_segmentSize;
    const unsigned owner=1+(m_asyncIndex[0]+m_numMachines-1-unsigned(slot))%m_numMachines;
    // Log route changes and validation failures.
    if(true) {
      const uint64_t key=uint64_t(state.routing.valid)|(uint64_t(state.routing.fail)<<1)|
        (uint64_t(state.routing.local)<<9)|(uint64_t(state.routing.machines)<<17)|
        (uint64_t(state.routing.humans)<<25)|(uint64_t(state.routing.total)<<33)|
        (uint64_t(state.routing.authority)<<41)|(uint64_t(matched)<<49);
      static uint64_t routedKey=~uint64_t(0);
      if(key!=routedKey) {
        routedKey=key;state.routingLogged=true;
        InfoLog("%s routing: native=%u/%u HLE=%u/%u humans=%u cars=%u CPU-authority=%u matched=%d fail=0x%02x",
          m_gameInfo.name.c_str(),unsigned(state.routing.local),unsigned(state.routing.machines),
          unsigned(m_asyncIndex[0]),unsigned(m_numMachines),unsigned(state.routing.humans),
          state.routing.total,unsigned(state.routing.authority),int(matched),unsigned(state.routing.fail));
      }
    }
    // Predict only the local receive copy; raw bytes and local echo stay intact.
    double sampleAgeFrames=(slot<m_asyncLatestAgeUs.size() && slot<m_asyncLatestUncertaintyUs.size() &&
      m_asyncLatestUncertaintyUs[slot]<60000000ULL)
      ? double(m_asyncLatestAgeUs[slot])/FrameDurationUs() : double(m_gameNet.m_asyncSnapshotAge);
    std::vector<uint8_t> udpStorage;double uncertaintyFrames=0;
    const uint8_t* presentation=PresentationSource(slot,udpStorage,sampleAgeFrames,uncertaintyFrames);
    const auto result=state.predictors[slot].Apply(presentation,m_segmentSize,
      CommRAM+0x100+m_segmentSize+offset,m_gameNet.m_predictionFrame,sampleAgeFrames,state.routing,matched?owner:0);
    if(result.human && !state.humanLogged) {
      state.humanLogged=true;
      const unsigned phase=m_asyncLatest.size()>=size_t(offset)+m_segmentSize?unsigned(Scud::Read8(m_asyncLatest.data()+offset,0x1f)):0u;
      InfoLog("%s human prediction active: verified remote cabinet=%u packet phase=%u",m_gameInfo.name.c_str(),owner,phase);
    }
    if(result.cpus && !state.cpuLogged) {
      state.cpuLogged=true;InfoLog("%s CPU progress prediction active: %u verified authority records",m_gameInfo.name.c_str(),result.cpus);
    }
    // Log phases 19–21, where the remote cars are moving before and after GO.
    if(matched && m_asyncLatest.size()>=size_t(offset)+m_segmentSize) {
      const unsigned phase=unsigned(Scud::Read8(m_asyncLatest.data()+offset,0x1f));
      // Selection phases are rejected by DecodeHeader.
      if(phase!=state.lastPhase) {
        state.lastPhase=phase;
        if(Scud::InRacePhase(phase))
          InfoLog("%s prediction phase=%u%s",m_gameInfo.name.c_str(),phase,
            result.human?" (predicting)":" (waiting for remote velocity)");
        else
          InfoLog("%s prediction idle: packet phase=%u is not an in-race phase",m_gameInfo.name.c_str(),phase);
      }
    }
  }
}
