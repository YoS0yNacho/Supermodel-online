// Read-only native middle orientation component; not a prediction hook.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_SGT24H_ORIENTATION_H
#define INCLUDED_SGT24H_ORIENTATION_H
#include "Position.h"
namespace Sgt24h {
// Body +0x50 == actor +0x808 -> full row +0x3c; compact +0x1aa.
// 0x2e56c..0x2e5c0 uses modular 16-bit differences. Axis naming is unverified.
inline bool ReadFullOrientation(const uint8_t* row,size_t size,unsigned index,uint16_t& out) {
 if(!row || size!=RowBytes || index>=FullCapacity)return false;
 const unsigned offset=0x3c+index*FullStride;
 out=uint16_t(unsigned(row[offset])|(unsigned(row[offset+1])<<8));
 return true;
}
inline bool ReadFirstOrientation(const uint8_t* row,size_t size,uint16_t& out) {
 return ReadFullOrientation(row,size,0,out);
}
inline bool ReadCompactOrientation(const uint8_t* row,size_t size,unsigned index,uint16_t& out) {
 if(!row || size!=RowBytes || index>=CompactCapacity)return false;
 const unsigned offset=CompactBase+index*CompactStride+0xe;
 out=uint16_t(unsigned(row[offset])|(unsigned(row[offset+1])<<8));
 return true;
}
// Shortest modular delta. Exact half-turn deterministically chooses -32768.
// This is a math primitive, not a claim about physical angular velocity.
constexpr int OrientationDelta(uint16_t from,uint16_t to) {
 const unsigned delta=(unsigned(to)+65536u-unsigned(from))&65535u;
 return delta>=32768u?int(delta)-65536:int(delta);
}
}
#endif
