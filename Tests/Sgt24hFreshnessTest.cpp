#include "Network/Games/Sgt24h/Freshness.h"
#include <cassert>
using namespace Sgt24h;
int main() {
 Freshness f;assert(!f.Current({},1,100));assert(!f.Applied(1));
 f.Begin(1);assert(f.Token().generation==0);assert(!f.Current({},1,100));
 assert(f.Applied(100));auto first=f.Token();assert(first.epoch==1 && first.generation==1);
 for(unsigned i=0;i<10000;++i) {
  assert(f.Current(first,100,0));assert(f.Token().generation==1);
 }
 assert(f.Current(first,200,100));assert(!f.Current(first,201,100));
 assert(!f.Current(first,99,100));assert(!f.Current({2,1},100,100));
 assert(!f.Current({1,2},100,100));
 assert(f.Applied(100));auto second=f.Token();assert(second.generation==2);
 assert(!f.Current(first,100,100));assert(f.Current(second,100,0));
 assert(!f.Applied(99));assert(!f.Current(second,100,100));assert(!f.Applied(101));
 f.Begin(2);assert(f.Applied(1));assert(!f.Current(second,1,100));
 auto third=f.Token();f.Clear();assert(!f.Current(third,1,100));
 f.Begin(0);assert(!f.Applied(1));
 f.Begin(3);assert(!f.Applied(0));assert(f.Token().generation==0);
 f.Begin(4);assert(f.Applied(UINT64_MAX-1));auto last=f.Token();
 assert(f.Current(last,UINT64_MAX,1));assert(!f.Current(last,0,UINT64_MAX));
}
