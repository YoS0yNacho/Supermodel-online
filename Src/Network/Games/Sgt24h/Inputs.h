// Verified SGT cabinet-row input fields, not a runtime input override.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_SGT24H_INPUTS_H
#define INCLUDED_SGT24H_INPUTS_H
#include "Profile.h"
#include <cstddef>
namespace Sgt24h {
// Offset names preserve wire/state identity. Source trace identifies:
// baseBC: processed steering (analog channel 0 -> 510e9c).
// baseC0: processed accelerator (channel 1 -> 510eb4).
// baseEC: processed brake (channel 2 -> 510ec8, with mode overrides).
// baseE4: handler result from ed610; exact gear semantics not yet asserted.
// These are NOT raw host axes; calibration/filtering and overrides matter.
struct Inputs { int baseBC; unsigned baseC0,baseE4,baseEC; };
// ROM baa40 packs base fields; baa00 imports into transmitted state.
inline bool ReadInputs(const uint8_t* row,std::size_t size,Inputs& out) {
 if(!row || size!=RowBytes)return false;
 const unsigned raw=unsigned(row[0x26])|(unsigned(row[0x27])<<8);
 const Inputs value{raw>=32768u?int(raw)-65536:int(raw),
                    row[0x28],row[0x29],row[0x2a]};
 out=value;return true;
}
}
#endif
