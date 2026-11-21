// Conservative fresh-match extraction from the native per-frame row scan.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_SGT24H_MEMBERSHIP_H
#define INCLUDED_SGT24H_MEMBERSHIP_H
#include "Profile.h"
#include <array>
#include <cstddef>
namespace Sgt24h {
struct MembershipRow { uint8_t state,group; };
// Decode only the active RX row span, beginning at comm RX (0x2380).
// Exact count*0x380 bytes, not the whole link frame and not guest 0x47c
// records. Unused cabinet entries are cleared on success. No guest writes.
inline bool ReadRingMembership(const uint8_t* rx,std::size_t size,
                              unsigned local,unsigned count,
                              std::array<MembershipRow,Cabinets>& out) {
 if(!rx || count<2 || count>Cabinets || local>=count || size!=count*RowBytes)return false;
 std::array<MembershipRow,Cabinets> value{};
 for(unsigned row=0;row<count;++row) {
  const unsigned cabinet=RowSeat(local,row,count);
  value[cabinet]={rx[row*RowBytes+2],rx[row*RowBytes+3]};
 }
 out=value;return true;
}
// Read-only matching predicate from ba888..ba8a4. Caller supplies already
// decoded cabinet metadata, in cabinet order (NOT comm ring RX order).
// start is the native scan cursor; output is updated only on a fresh match.
// Unlike native r6 retention, failure never reports the prior slot as valid.
// This is NOT a complete binding, race-validity, authority or freshness test.
inline bool FindFullSlot(const std::array<MembershipRow,Cabinets>& rows,
                         unsigned count,unsigned selected,unsigned start,
                         unsigned& out) {
 if(count<2 || count>Cabinets || selected>=count || start>=count)return false;
 const auto group=rows[selected].group;
 for(unsigned i=start;i<count;++i) {
  if((rows[i].state==3 || rows[i].state==4) && rows[i].group==group) {
   out=i;return true;
  }
 }
 return false;
}
}
#endif
