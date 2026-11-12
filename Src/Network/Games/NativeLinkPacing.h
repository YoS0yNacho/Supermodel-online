// Keep native setup/selection exchanges off the low-rate racing pacer.
// GPL-3.0-or-later. Read-only packet inspection, emulation thread only.
#ifndef INCLUDED_NATIVE_LINK_PACING_H
#define INCLUDED_NATIVE_LINK_PACING_H
#include "Daytona2/Daytona2Prediction.h"
#include "Srally2/Prediction.h"
namespace GameNetwork {
inline bool NeedsNativeSetupPacing(const std::string& game) {
  return game=="dayto2pe" || game=="srally2";
}
struct NativeRaceContext { uint64_t shared=0, identity=0; };
inline bool ReadNativeRaceContext(const std::string& game,const uint8_t* raw,
 size_t size,unsigned cabinet,unsigned machines,NativeRaceContext& out) {
  if(!raw || machines<2 || cabinet>=machines) return false;
  if(game=="dayto2pe") {
    if(machines>Dayto2pe::MaxCabinets) return false;
    Dayto2pe::Sample sample;
    if(!Dayto2pe::Decode(raw,size,sample,Dayto2pe::PowerEdition)) return false;
    out={Dayto2pe::PowerEdition.raceState,0};return true;
  }
  if(game=="srally2") {
    if(size!=Srally2::PacketSize || machines>Srally2::MaxCabinets) return false;
    const uint32_t playable=Srally2::Read32(raw,0x04),car=Srally2::Read32(raw,0x20);
    // Phase10 also covers loading/countdown: timer must have passed native GO.
    // Phase11 is results, not steady racing. No guessed wall-clock launch timer.
    if(Srally2::Read32(raw,0x00)!=cabinet || playable>=machines ||
       Srally2::Read32(raw,0x08)!=1 || Srally2::Read32(raw,0x0c)!=1 ||
       Srally2::Read32(raw,0x18)!=10 || Srally2::Read32(raw,0x74)==0 ||
       car>=32 || Srally2::Read32(raw,0xc4)>Srally2::MaxCpuCars) return false;
    out={Srally2::Read32(raw,0x10),(uint64_t(car)<<32)|playable};return true;
  }
  return false;
}
struct NativeLinkPacing {
  static constexpr unsigned StableRaceFrames=60;
  static constexpr uint64_t MaxPeerSilenceUs=2000000;
  uint64_t context=0;
  unsigned stableFrames=0;
  bool known=false;
  void Reset() {context=0;stableFrames=0;known=false;}
  unsigned Select(unsigned negotiated,bool allRacing,uint64_t key) {
    if(!allRacing) {Reset();return 60;}
    if(!known || context!=key) {context=key;stableFrames=0;known=true;}
    if(stableFrames<StableRaceFrames) ++stableFrames;
    return stableFrames>=StableRaceFrames ? negotiated : 60;
  }
};
}
#endif
