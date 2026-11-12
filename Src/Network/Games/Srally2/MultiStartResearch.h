// Experimental ROM-free helpers, not evidence of multi-cabinet operation.
#ifndef INCLUDED_SRALLY2_MULTI_START_RESEARCH_H
#define INCLUDED_SRALLY2_MULTI_START_RESEARCH_H
#include "StartAssistResearch.h"
#include <cstddef>
#include <cstdint>
namespace Srally2 { namespace Research {
// Validate every cabinet slot, including the local echo, before trusting phases.
inline bool RingEligible(const uint8_t* ring,size_t size,unsigned count,unsigned self) {
 if(!ring || count<2 || count>4 || self>=count || size!=count*SegmentSize) return false;
 unsigned seen=0;
 for(unsigned i=0;i<count;++i){
  const auto* p=ring+i*SegmentSize; const auto id=Word(p+CabinetId);
  if(id>=count || (seen & (1u<<id))) return false;
  seen|=1u<<id;
  if(id!=self) { const auto state=Word(p+Phase); if(state!=8 && state!=10) return false; }
 }
 return seen==((1u<<count)-1);
}
inline const uint8_t* FindCoordinator(const uint8_t* ring,size_t size,unsigned count,unsigned self) {
 if(!ring || count<2 || count>4 || self==0 || self>=count || size!=count*SegmentSize) return nullptr;
 unsigned seen=0; const uint8_t* leader=nullptr;
 for(unsigned i=0;i<count;++i){
  const auto* p=ring+i*SegmentSize; const auto id=Word(p+CabinetId);
  if(id>=count || (seen & (1u<<id))) return nullptr;
  seen|=1u<<id; if(id==0) leader=p;
 }
 return seen==((1u<<count)-1) ? leader : nullptr;
}
inline bool MirrorParticipantMulti(uint8_t* out,size_t n,const uint8_t* ring,size_t size,unsigned count,unsigned self) {
 if(!out || n!=SegmentSize || Word(out+CabinetId)!=self || Word(out+Phase)!=10) return false;
 const auto* leader=FindCoordinator(ring,size,count,self);
 if(!leader || Word(leader+Coordinator)>3) return false;
 std::copy_n(leader+Counter,4,out+Counter);std::copy_n(leader+Coordinator,4,out+Acknowledgement);
 return true;
}
}}
#endif
