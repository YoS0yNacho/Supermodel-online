#include "Network/Games/Sgt24h/Orientation.h"
#include <array>
#include <cassert>
#include <cstring>
using namespace Sgt24h;
int main() {
 std::array<uint8_t,RowBytes> row{};
 auto put=[&](unsigned offset,uint32_t v) {
  for(unsigned j=0;j<4;++j)row[offset+j]=uint8_t(v>>(j*8));
 };
 for(unsigned slot=0;slot<FullCapacity;++slot) {
  for(unsigned c=0;c<3;++c) {
   float v=float(slot*10+c);uint32_t raw;std::memcpy(&raw,&v,4);
   put(PositionOffset+slot*FullStride+c*4,raw);
  }
  put(0x3c+slot*FullStride,0xabcd0000u+slot);
 }
 const auto original=row;
 Position p;uint16_t angle=0;
 for(unsigned slot=0;slot<FullCapacity;++slot) {
  assert(ReadFullPosition(row.data(),row.size(),slot,p));
  for(unsigned c=0;c<3;++c)assert(p.component[c]==float(slot*10+c));
  assert(ReadFullOrientation(row.data(),row.size(),slot,angle) && angle==slot);
 }
 assert(row==original);
 for(unsigned slot=0;slot<FullCapacity;++slot) {
  for(unsigned c=0;c<3;++c)for(uint32_t bad:{0x7f800000u,0xff800000u,0x7fc00000u}) {
   row=original;put(PositionOffset+slot*FullStride+c*4,bad);
   p={{11,22,33}};
   assert(!ReadFullPosition(row.data(),row.size(),slot,p));
   assert(p.component[0]==11 && p.component[1]==22 && p.component[2]==33);
   for(unsigned other=0;other<FullCapacity;++other)if(other!=slot) {
    Position q;assert(ReadFullPosition(row.data(),row.size(),other,q));
   }
  }
  for(unsigned raw=0;raw<65536;++raw) {
   put(0x3c+slot*FullStride,0xabcd0000u+raw);
   assert(ReadFullOrientation(row.data(),row.size(),slot,angle) && angle==raw);
  }
 }
 p={{11,22,33}};angle=123;
 for(unsigned slot:{4u,15u,~0u}) {
  assert(!ReadFullPosition(row.data(),row.size(),slot,p));
  assert(!ReadFullOrientation(row.data(),row.size(),slot,angle));
 }
 for(unsigned slot=0;slot<FullCapacity;++slot) {
  for(size_t len=0;len<RowBytes;++len) {
   assert(!ReadFullPosition(row.data(),len,slot,p));
   assert(!ReadFullOrientation(row.data(),len,slot,angle));
  }
  assert(!ReadFullPosition(nullptr,RowBytes,slot,p));
  assert(!ReadFullOrientation(nullptr,RowBytes,slot,angle));
  assert(!ReadFullPosition(row.data(),RowBytes+1,slot,p));
  assert(!ReadFullOrientation(row.data(),RowBytes+1,slot,angle));
 }
 assert(p.component[0]==11 && p.component[1]==22 && p.component[2]==33 && angle==123);
}
