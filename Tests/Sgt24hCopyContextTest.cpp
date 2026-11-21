#include "Network/Games/Sgt24h/CopyContext.h"
#include <cassert>
#include <initializer_list>
using namespace Sgt24h;
int main() {
 static_assert(RxCopyFinalLoadPC==0x21bfd0);
 for(unsigned n=2;n<=4;++n)for(unsigned local=0;local<n;++local)for(unsigned row=0;row<n;++row) {
  const uint32_t src=0x1a12380+row*RowBytes;
  const unsigned expected=(local+row+1)%n;
  const uint32_t dst=RxActors+expected*CabinetRecordStride;
  unsigned seat=99;
  auto valid=[&](uint32_t pc,uint32_t rip,uint32_t s,uint32_t d,uint32_t a,uint32_t mask) {
   return MatchRxCopy(local,n,pc,rip,s,d,a,mask,seat);
  };
  assert(valid(RxCopyFinalLoadPC,RxCopyReturnPC,src,dst,src+0x37c,~0u) && seat==expected);
  seat=99;
  for(uint32_t pc:{CopyCodeROM,0xbb720u,RxCopyFinalLoadPC-4,RxCopyFinalLoadPC+4})assert(!valid(pc,RxCopyReturnPC,src,dst,src+0x37c,~0u));
  for(uint32_t rip:{0xb95c8u,0xb96c0u,0u})assert(!valid(RxCopyFinalLoadPC,rip,src,dst,src+0x37c,~0u));
  for(unsigned off=0;off<RowBytes;++off)if(off!=0x37c)assert(!valid(RxCopyFinalLoadPC,RxCopyReturnPC,src,dst,src+off,~0u));
  for(uint32_t bad:{0u,src+1,0x1a02380u,0xffffffffu})assert(!valid(RxCopyFinalLoadPC,RxCopyReturnPC,bad,dst,src+0x37c,~0u));
  assert(!valid(RxCopyFinalLoadPC,RxCopyReturnPC,src,dst+4,src+0x37c,~0u));
  assert(!valid(RxCopyFinalLoadPC,RxCopyReturnPC,src,dst,src+0x37c,0xffff));
  assert(seat==99);
 }
 for(unsigned n:{0u,1u,5u,~0u}) {
  unsigned seat=99;assert(!MatchRxCopy(0,n,RxCopyFinalLoadPC,RxCopyReturnPC,0x1a12380,RxActors,0x1a126fc,~0u,seat));assert(seat==99);
 }
}
