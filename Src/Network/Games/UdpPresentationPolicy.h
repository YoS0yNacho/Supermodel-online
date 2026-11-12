// UDP may supply predictor inputs only inside a matching TCP-owned race/owner
// context. It never replaces native control bytes or the ring self echo.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_UDP_PRESENTATION_POLICY_H
#define INCLUDED_UDP_PRESENTATION_POLICY_H
#include "Daytona2/Daytona2Prediction.h"
#include "Scud/Prediction.h"
#include "Srally2/Prediction.h"
#include "Lemans24/Prediction.h"
namespace GameNetwork {
inline bool PresentationCompatible(const std::string& game,const uint8_t* a,const uint8_t* b,size_t n,unsigned remote,unsigned local,unsigned machines) {
 if(!a || !b) return false;
 if(Lemans24::IsSupportedGame(game)) {
  Lemans24::HeaderSample x{},y{};
  if(!Lemans24::DecodeHeader(a,n,remote,local,machines,x) || !Lemans24::DecodeHeader(b,n,remote,local,machines,y)) return false;
  if(x.raceSubstate!=y.raceSubstate || x.raceSub!=y.raceSub || x.raceMode!=y.raceMode || x.senderIndex!=y.senderIndex || x.commMode!=y.commMode || x.numCpuCars!=y.numCpuCars) return false;
  if(Lemans24::Read16(a,Lemans24::HumanPoseOffset)!=Lemans24::Read16(b,Lemans24::HumanPoseOffset)) return false;
  for(unsigned i=0;i<x.numCpuCars;++i) if(Lemans24::Read16(a,Lemans24::CpuPoseBase+i*Lemans24::PoseStride)!=Lemans24::Read16(b,Lemans24::CpuPoseBase+i*Lemans24::PoseStride)) return false;
  return true;
 }
 if(Srally2::IsSupportedGame(game)) {
  Srally2::HeaderSample x{},y{};
  if(!Srally2::DecodeHeader(a,n,remote,local,machines,x) || !Srally2::DecodeHeader(b,n,remote,local,machines,y)) return false;
  if(x.playableIndex!=y.playableIndex || x.raceStage!=y.raceStage || x.linkPhase!=y.linkPhase || x.carId!=y.carId || x.numCpuCars!=y.numCpuCars) return false;
  for(unsigned i=0;i<=x.numCpuCars;++i) {
   const size_t p=i ? Srally2::CpuPoseBase+(i-1)*Srally2::PoseStride : Srally2::HumanPoseOffset;
   if((Srally2::Read32(a,p+0x44)&0xf03fc000u)!=(Srally2::Read32(b,p+0x44)&0xf03fc000u)) return false;
  }
  return true;
 }
 if(Scud::IsSupportedGame(game)) {
  uint32_t x,y;if(!Scud::DecodeHeader(a,n,x) || !Scud::DecodeHeader(b,n,y)) return false;
  if(Scud::Read8(a,0x1f)!=Scud::Read8(b,0x1f) || Scud::Read8(a,0x1c)!=Scud::Read8(b,0x1c) || Scud::Read8(a,0x48)!=Scud::Read8(b,0x48) || (Scud::Read32(a,0x44)&1)!=(Scud::Read32(b,0x44)&1)) return false;
  for(unsigned i=0;i<Scud::MaxCars;++i) if((Scud::Read16(a,0x124+i*4)&1)!=(Scud::Read16(b,0x124+i*4)&1)) return false;
  return true;
 }
 const auto* p=Dayto2pe::GetProfile(game);
 if(p) {
  Dayto2pe::Sample x,y;if(!Dayto2pe::Decode(a,n,x,*p) || !Dayto2pe::Decode(b,n,y,*p)) return false;
  if((Dayto2pe::Read32(a,p->classOffset)&p->classMask)!=(Dayto2pe::Read32(b,p->classOffset)&p->classMask) || (Dayto2pe::Read32(a,0x64)&0x81)!=(Dayto2pe::Read32(b,0x64)&0x81)) return false;
  for(unsigned i:{0u,2u}) if(a[(p->compactHeader+i)^2]!=b[(p->compactHeader+i)^2]) return false;
  if((a[(p->compactHeader+3)^2]&1)!=(b[(p->compactHeader+3)^2]&1)) return false;
  return true;
 }
 return false;
}
}
#endif
