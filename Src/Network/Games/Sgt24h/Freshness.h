// Conservative applied-frame freshness model, not yet attached to transport.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_SGT24H_FRESHNESS_H
#define INCLUDED_SGT24H_FRESHNESS_H
#include <cstdint>
namespace Sgt24h {
struct FrameToken { uint64_t epoch=0,generation=0; };
class Freshness {
 uint64_t epoch=0,generation=0,appliedUs=0;
 bool active=false;
public:
 // Nonzero, session-unique epoch supplied by integration. Never reuse it
 // across reset/load/disconnect/reconnect; this helper cannot enforce that.
 void Begin(uint64_t nextEpoch) {epoch=nextEpoch;generation=appliedUs=0;active=epoch!=0;}
 void Clear() {active=false;generation=appliedUs=0;}
 // Invoke only AFTER validated, complete network data is applied to RX.
 // Repeated ROM copies, FC control and failed reads must not call this.
 bool Applied(uint64_t now) {
  if(!active || !now || (generation && now<appliedUs) || generation==UINT64_MAX) {
   Clear();return false;
  }
  appliedUs=now;++generation;return true;
 }
 FrameToken Token() const {return active && generation?FrameToken{epoch,generation}:FrameToken{};}
 bool Current(FrameToken token,uint64_t now,uint64_t maxAge) const {
  return active && generation && token.epoch==epoch && token.generation==generation &&
         now>=appliedUs && now-appliedUs<=maxAge;
 }
};
}
#endif
