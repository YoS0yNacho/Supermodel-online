#include "Network/Games/Sgt24h/Membership.h"
#include <cassert>
#include <limits>
using namespace Sgt24h;
int main() {
 // Independently exhaust eligible-state and same-group masks for every
 // cabinet count, selected authority and starting scan position.
 for(unsigned count=2;count<=4;++count)
  for(unsigned selected=0;selected<count;++selected)
   for(unsigned active=0;active<16;++active)
    for(unsigned groups=0;groups<16;++groups)
     for(unsigned start=0;start<=count;++start) {
      std::array<MembershipRow,Cabinets> rows{};
      for(unsigned i=0;i<Cabinets;++i)
       rows[i]={uint8_t(active&(1u<<i)?(i%2?4:3):8),uint8_t(groups&(1u<<i)?0x91:0x37)};
      auto original=rows;unsigned expected=count;
      for(unsigned i=start;i<count;++i)
       if((active&(1u<<i)) && bool(groups&(1u<<i))==bool(groups&(1u<<selected))) {expected=i;break;}
      unsigned out=123;
      bool found=FindFullSlot(rows,count,selected,start,out);
      assert(found==(expected<count));assert(out==(found?expected:123));
      for(unsigned i=0;i<Cabinets;++i)assert(rows[i].state==original[i].state && rows[i].group==original[i].group);
     }
 // All state/group bytes, including rejection of init-only state8.
 for(unsigned state=0;state<256;++state)for(unsigned group=0;group<256;++group) {
  std::array<MembershipRow,Cabinets> rows{};
  rows[0]={0,0xa5};rows[1]={uint8_t(state),uint8_t(group)};
  unsigned out=123;
  bool expected=(state==3 || state==4) && group==0xa5;
  assert(FindFullSlot(rows,2,0,1,out)==expected);
  assert(out==(expected?1u:123u));
 }
 std::array<MembershipRow,Cabinets> rows{};for(auto& r:rows)r={3,7};
 for(unsigned count:{0u,1u,5u,~0u}) {
  unsigned out=123;assert(!FindFullSlot(rows,count,0,0,out) && out==123);
 }
 for(unsigned bad:{4u,5u,~0u}) {
  unsigned out=123;assert(!FindFullSlot(rows,4,bad,0,out) && out==123);
  assert(!FindFullSlot(rows,4,0,bad,out) && out==123);
 }
 // Exhausted scan must not reissue a previous match.
 unsigned out=123;assert(FindFullSlot(rows,4,0,3,out) && out==3);
 assert(!FindFullSlot(rows,4,0,4,out) && out==3);
 rows[3].state=8;assert(!FindFullSlot(rows,4,0,3,out) && out==3);
 // The unchanged output must only be consumed when return==true.
}
