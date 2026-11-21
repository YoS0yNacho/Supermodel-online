// Source model of ROM state-2 authority selection; not a runtime hook.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_SGT24H_SELECTION_H
#define INCLUDED_SGT24H_SELECTION_H
#include "Profile.h"
#include <array>
namespace Sgt24h {
struct SelectionRow { uint8_t state,flags; };
struct Selection { unsigned selected,groupCabinet,participants; };
// ROM ba600..ba6cc scans four cabinet slots, irrespective of link count.
// Called only for state 2 in native code. Row flags is byte +0x1a.
// A zero-match scan preserves previous state, not a disconnect indication.
inline Selection SelectStateTwo(const std::array<SelectionRow,Cabinets>& rows,
                                Selection previous) {
 unsigned count=0;
 for(unsigned i=0;i<Cabinets;++i) {
  if(rows[i].state!=2 || !(rows[i].flags&1))continue;
  if(count==0)previous.selected=previous.groupCabinet=i;
  previous.participants=++count;
 }
 return previous;
}
}
#endif
