#include "Network/Games/Sgt24h/Inputs.h"
#include <array>
#include <cassert>
int main() {
 using namespace Sgt24h;
 std::array<uint8_t,RowBytes> row{};
 Inputs out{123,45,67,89};
 for(unsigned raw=0;raw<65536;++raw) {
  row[0x26]=uint8_t(raw);row[0x27]=uint8_t(raw>>8);
  row[0x28]=uint8_t(raw);row[0x29]=uint8_t(raw>>8);row[0x2a]=uint8_t(~raw);
  assert(ReadInputs(row.data(),row.size(),out));
  assert(out.baseBC==(raw>=32768?int(raw)-65536:int(raw)));
  assert(out.baseC0==(raw&255) && out.baseE4==(raw>>8) && out.baseEC==((~raw)&255));
 }
 auto before=row;out={123,45,67,89};
 for(std::size_t n=0;n<RowBytes;++n)assert(!ReadInputs(row.data(),n,out));
 assert(!ReadInputs(nullptr,RowBytes,out));
 assert(!ReadInputs(row.data(),RowBytes+1,out));
 assert(out.baseBC==123 && out.baseC0==45 && out.baseE4==67 && out.baseEC==89);
 assert(before==row);
}
