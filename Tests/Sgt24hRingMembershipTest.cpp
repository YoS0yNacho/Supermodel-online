#include "Network/Games/Sgt24h/Membership.h"
#include <cassert>
using namespace Sgt24h;
int main() {
 std::array<uint8_t,FrameBytes> rx{};
 std::array<MembershipRow,Cabinets> out{};
 auto unchanged=[&]() {for(const auto& r:out)assert(r.state==91 && r.group==72);};
 for(unsigned count=2;count<=4;++count)for(unsigned local=0;local<count;++local) {
  for(unsigned row=0;row<count;++row) {
   const unsigned seat=(local+1+row)%count;
   rx[row*RowBytes+2]=uint8_t(3+(seat&1));
   rx[row*RowBytes+3]=uint8_t(100+seat);
  }
  auto original=rx;
  assert(ReadRingMembership(rx.data(),count*RowBytes,local,count,out));
  for(unsigned seat=0;seat<Cabinets;++seat) {
   assert(out[seat].state==(seat<count?3+(seat&1):0));
   assert(out[seat].group==(seat<count?100+seat:0));
  }
  assert(rx==original);
  // Integration with matcher: each unique group selects only its cabinet.
  for(unsigned selected=0;selected<count;++selected) {
   unsigned slot=99;
   assert(FindFullSlot(out,count,selected,0,slot) && slot==selected);
  }
  for(auto& r:out)r={91,72};
  for(size_t n=0;n<=FrameBytes+1;++n)if(n!=count*RowBytes) {
   assert(!ReadRingMembership(rx.data(),n,local,count,out));unchanged();
  }
  assert(!ReadRingMembership(nullptr,count*RowBytes,local,count,out));unchanged();
 }
 for(unsigned count:{0u,1u,5u,~0u}) {
  assert(!ReadRingMembership(rx.data(),0,0,count,out));unchanged();
 }
 for(unsigned local:{4u,5u,~0u}) {
  assert(!ReadRingMembership(rx.data(),FrameBytes,local,4,out));unchanged();
 }
}
