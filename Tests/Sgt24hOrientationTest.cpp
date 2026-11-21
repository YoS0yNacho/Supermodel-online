#include "Network/Games/Sgt24h/Orientation.h"
#include <array>
#include <cassert>
using namespace Sgt24h;
int main() {
 assert(OrientationDelta(65535,0)==1 && OrientationDelta(0,65535)==-1);
 assert(OrientationDelta(0,32768)==-32768);
 for(unsigned from=0;from<65536;++from)for(int step:{-32768,-32767,-10,-1,0,1,10,32767}) {
  const auto to=uint16_t(int(from)+step);
  assert(OrientationDelta(uint16_t(from),to)==step);
 }
 std::array<uint8_t,RowBytes> row{};
 uint16_t value=123;
 for(unsigned raw=0;raw<65536;++raw) {
  row[0x3c]=uint8_t(raw);row[0x3d]=uint8_t(raw>>8);
  row[0x3e]=0xab;row[0x3f]=0xcd; // modulo interpretation, not signed float
  assert(ReadFirstOrientation(row.data(),row.size(),value) && value==raw);
 }
 for(unsigned i=0;i<CompactCapacity;++i) {
  const unsigned o=CompactBase+i*CompactStride+0xe;
  row[o]=0xfe;row[o+1]=0xff;
  assert(ReadCompactOrientation(row.data(),row.size(),i,value) && value==65534);
 }
 auto before=row;value=123;
 for(size_t n=0;n<RowBytes;++n) {
  assert(!ReadFirstOrientation(row.data(),n,value) && value==123);
  assert(!ReadCompactOrientation(row.data(),n,0,value) && value==123);
 }
 for(unsigned i:{15u,16u,~0u})assert(!ReadCompactOrientation(row.data(),row.size(),i,value) && value==123);
 assert(!ReadFirstOrientation(nullptr,RowBytes,value));
 assert(!ReadCompactOrientation(nullptr,RowBytes,0,value));
 assert(!ReadFirstOrientation(row.data(),RowBytes+1,value));
 assert(!ReadCompactOrientation(row.data(),RowBytes+1,0,value));
 assert(row==before && value==123);
}
