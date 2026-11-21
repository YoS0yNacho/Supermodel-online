#include "Network/Games/Sgt24h/Position.h"
#include <array>
#include <cassert>
using namespace Sgt24h;
int main() {
 std::array<uint8_t,RowBytes> row{};
 auto put=[&](unsigned i,uint32_t word){for(unsigned j=0;j<4;++j)row[PositionOffset+4*i+j]=uint8_t(word>>(8*j));};
 put(0,0x3f800000);put(1,0xc0200000);put(2,0x42c80000);
 auto original=row;Position p;
 assert(ReadFirstPosition(row.data(),row.size(),p));
 assert(p.component[0]==1.0f && p.component[1]==-2.5f && p.component[2]==100.0f);
 assert(row==original);
 const auto before=p;
 for(size_t n=0;n<RowBytes;++n)assert(!ReadFirstPosition(row.data(),n,p));
 assert(!ReadFirstPosition(row.data(),RowBytes+1,p));
 assert(!ReadFirstPosition(nullptr,RowBytes,p));
 for(unsigned i=0;i<3;++i)for(uint32_t bad:{0x7f800000u,0xff800000u,0x7fc00000u,0x7f800001u}) {
  row=original;put(i,bad);assert(!ReadFirstPosition(row.data(),row.size(),p));
  for(unsigned c=0;c<3;++c)assert(p.component[c]==before.component[c]);
 }
 // Exhaust every signed compact middle component and every bounded slot.
 static_assert(CompactCapacity==15);
 row.fill(0);
 for(unsigned slot=0;slot<CompactCapacity;++slot) {
  const unsigned base=CompactBase+slot*CompactStride;
  row[base+2]=0x80;row[base+3]=0x3f; // +1
  row[base+10]=0x20;row[base+11]=0xc0; // -2.5
  for(unsigned raw=0;raw<65536;++raw) {
   row[base+4]=uint8_t(raw);row[base+5]=uint8_t(raw>>8);
   assert(ReadCompactPosition(row.data(),row.size(),slot,p));
   assert(p.component[0]==1 && p.component[2]==-2.5f);
   const float expected=float(raw>=32768?int(raw)-65536:int(raw))/100.0f;
   assert(p.component[1]==expected);
  }
 }
 auto compactOriginal=row;
 for(unsigned slot:{15u,16u,~0u})assert(!ReadCompactPosition(row.data(),row.size(),slot,p));
 for(size_t n=0;n<RowBytes;++n)assert(!ReadCompactPosition(row.data(),n,0,p));
 assert(!ReadCompactPosition(nullptr,RowBytes,0,p));
 assert(!ReadCompactPosition(row.data(),RowBytes+1,0,p));
 for(unsigned offset:{0u,8u})for(uint32_t raw:{0x7f800000u,0xff800000u,0x7fc00000u}) {
  row=compactOriginal;auto saved=p;
  for(unsigned j=0;j<4;++j)row[CompactBase+offset+j]=uint8_t(raw>>(8*j));
  auto badInput=row;
  assert(!ReadCompactPosition(row.data(),row.size(),0,p));
  assert(row==badInput);
  for(unsigned c=0;c<3;++c)assert(p.component[c]==saved.component[c]);
 }
 row.fill(0);assert(ReadFirstPosition(row.data(),row.size(),p));
 // Zero is allowed as raw data, not interpreted as an active race.
 assert(p.component[0]==0 && p.component[1]==0 && p.component[2]==0);
}
