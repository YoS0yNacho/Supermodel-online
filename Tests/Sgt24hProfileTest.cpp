#include "Network/Games/Sgt24h/Profile.h"
#include <cassert>
int main() {
 using namespace Sgt24h;
 static_assert(FrameBytes==Cabinets*RowBytes);
 static_assert(Rx==0x2380);
 static_assert(CarActorStride==3*CarStateBytes);
 static_assert(StateCabinetSlot+4==CarStateBytes);
 static_assert(ReceivedCabinetSlot+4==CarActorStride);
 static_assert(CarActorBase+DecodedCabinetSlot==0x518bbc);
 static_assert(CarActorBase+ReceivedCabinetSlot==0x519374);
 static_assert(LocalActorType==0 && LinkedPeerActorType==1 && CompactActorType==2);
 static_assert(LocalActorIndex!=Seat && SelectedCabinet!=Seat);
 static_assert(CarActorBase+TransmittedState==0x518408);
 static_assert(CarActorBase+ReceivedState==0x518bc0);
 static_assert(TransmittedState+StatePosition==0x7e0);
 static_assert(ReceivedState+StatePosition==0xf98);
 static_assert(TransmittedState+0x50==0x808);
 static_assert(ReceivedState+0x50==0xfc0);
 static_assert(CabinetRecordStride!=CarActorStride);
 static_assert(CopiedActorBytes==RowBytes && CopiedActorBytes<ActorStride);
 assert(RowSeat(0,0,1)==Cabinets && RowSeat(4,0,4)==Cabinets);
 assert(RowSeat(0,4,4)==Cabinets);
 for(unsigned n=2;n<=4;++n)for(unsigned local=0;local<n;++local) {
  unsigned seen=0;
  for(unsigned row=0;row<n;++row) {
   unsigned seat=RowSeat(local,row,n);
   assert(seat<n && !(seen&(1u<<seat)));seen|=1u<<seat;
   assert((seat==local)==(row==n-1));
  }
  assert(seen==(1u<<n)-1);
 }
 assert(EchoOffset(0)==0 && EchoOffset(1)==0 && EchoOffset(5)==0);
 for(unsigned n=2;n<=Cabinets;++n) {
  assert(EchoOffset(n)==Tx+n*RowBytes);
  assert(EchoOffset(n)+RowBytes<=0x4000);
 }
}
