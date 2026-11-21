#include "Network/Games/Sgt24h/Selection.h"
#include <cassert>
using namespace Sgt24h;
int main() {
 for(unsigned mask=0;mask<16;++mask) {
  std::array<SelectionRow,Cabinets> rows{};
  unsigned first=Cabinets,count=0;
  for(unsigned i=0;i<Cabinets;++i) {
   rows[i]={2,uint8_t((mask>>i)&1)};
   if(mask&(1u<<i)){if(first==Cabinets)first=i;++count;}
  }
  const auto original=rows;
  for(unsigned selected=0;selected<Cabinets;++selected)
   for(unsigned group=0;group<Cabinets;++group)
    for(unsigned participants=0;participants<=Cabinets;++participants) {
     Selection prior{selected,group,participants};
     const auto result=SelectStateTwo(rows,prior);
     assert(result.selected==(count?first:selected));
     assert(result.groupCabinet==(count?first:group));
     assert(result.participants==(count?count:participants));
    }
  for(unsigned i=0;i<Cabinets;++i)assert(rows[i].state==original[i].state && rows[i].flags==original[i].flags);
 }
 // Exhaust byte values in each slot; only state==2 and bit0 count.
 for(unsigned slot=0;slot<Cabinets;++slot)
  for(unsigned state=0;state<256;++state)
   for(unsigned flags=0;flags<256;++flags) {
    std::array<SelectionRow,Cabinets> rows{};
    rows[slot]={uint8_t(state),uint8_t(flags)};
    const auto result=SelectStateTwo(rows,{3,2,4});
    const bool match=state==2 && (flags&1);
    assert(result.selected==(match?slot:3));
    assert(result.groupCabinet==(match?slot:2));
    assert(result.participants==(match?1u:4u));
   }
 // Authority moves only when the next scan contains qualifying members.
 std::array<SelectionRow,Cabinets> rows{};
 rows[1]={2,1};rows[3]={2,1};
 auto s=SelectStateTwo(rows,{0,0,4});assert(s.selected==1 && s.participants==2);
 rows[1]={3,1};s=SelectStateTwo(rows,s);assert(s.selected==3 && s.participants==1);
 rows[3]={2,0};s=SelectStateTwo(rows,s);assert(s.selected==3 && s.participants==1);
}
