// Read-only full/compact spatial samples. No network or guest-memory writes.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_SGT24H_POSITION_H
#define INCLUDED_SGT24H_POSITION_H
#include "Profile.h"
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <initializer_list>
namespace Sgt24h {
// 0xbace0 serializer: actor +7e0/7e4/7e8 -> row +2c/30/34.
// 0xbaa80 reader: row -> actor +f98/f9c/fa0; 0x45464 uses subr.
// Axis labels/units, race identity and ownership are deliberately not inferred.
constexpr unsigned PositionOffset=0x2c;
// Full records occupy [0x2c,0x19c); count is layout capacity, not ownership.
constexpr unsigned FullStride=0x5c, FullCapacity=4;
static_assert(PositionOffset+FullStride*FullCapacity==0x19c);
struct Position { float component[3]={}; };
inline bool ReadFullPosition(const uint8_t* row,size_t size,unsigned index,Position& out) {
 static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559);
 if(!row || size!=RowBytes || index>=FullCapacity)return false;
 Position sample;
 for(unsigned i=0;i<3;++i) {
  const uint8_t* p=row+PositionOffset+index*FullStride+4*i;
  const uint32_t word=uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);
  std::memcpy(&sample.component[i],&word,4);
  if(!std::isfinite(sample.component[i]))return false;
 }
 out=sample;
 return true;
}
inline bool ReadFirstPosition(const uint8_t* row,size_t size,Position& out) {
 return ReadFullPosition(row,size,0,out);
}
// 0xbb0d0 / 0xbaf50: compact records start at 0x19c, stride 0x20.
// Capacity below is a BUFFER bound only, not the active actor count.
constexpr unsigned CompactBase=0x19c, CompactStride=0x20;
constexpr unsigned CompactCapacity=(RowBytes-CompactBase)/CompactStride;
inline bool ReadCompactPosition(const uint8_t* row,size_t size,unsigned index,Position& out) {
 static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559);
 if(!row || size!=RowBytes || index>=CompactCapacity)return false;
 const uint8_t* p=row+CompactBase+index*CompactStride;
 Position sample;
 for(unsigned i:{0u,2u}) {
  const uint8_t* q=p+i*4;
  const uint32_t word=uint32_t(q[0])|(uint32_t(q[1])<<8)|(uint32_t(q[2])<<16)|(uint32_t(q[3])<<24);
  std::memcpy(&sample.component[i],&word,4);
  if(!std::isfinite(sample.component[i]))return false;
 }
 const unsigned raw=unsigned(p[4])|(unsigned(p[5])<<8);
 const int signedValue=raw>=0x8000?int(raw)-0x10000:int(raw);
 sample.component[1]=float(signedValue)/100.0f;
 out=sample;
 return true;
}
// A finite sample is NOT proof of race validity or permission to predict it.
}
#endif
