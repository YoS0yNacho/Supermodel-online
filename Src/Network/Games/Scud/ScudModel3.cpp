// Scud / Scud Plus verified native RAM snapshots. GPL-3.0-or-later.
#include "Supermodel.h"
#include "Model3/Model3.h"
#include "Prediction.h"
#include "Network/Games/NativeRaceMembership.h"

bool CModel3::HoldForScudNetwork()
{
  Scud::Routing route;
  // Follow the native object pointer; its base differs by ROM edition.
  route.local=Read8(0x10f002); route.machines=Read8(0x10f001);
  // Capture the native human roster and one-based cabinet owner map.
  route.humans=Read8(0x10f027); route.total=Read32(0x104018);
  for(unsigned i=0;i<8;++i) route.players[i]=Read8(0x10f028+i);
  const uint32_t header=Read32(0x10f008),base=Read32(0x1045a0);
  const bool addresses=header>=0x100000 && header<=0x7fffc0 && base>=0x100000 && base<=0x7f0000;
  if(addresses) route.authority=Read8(header+0x18);
  // Record validation failures for diagnostics.
  route.valid=true;
  if(!addresses) { route.valid=false; route.fail|=0x01; }
  if(!(route.machines>=2 && route.machines<=8)) { route.valid=false; route.fail|=0x02; }
  if(!(route.local>=1 && route.local<=route.machines)) { route.valid=false; route.fail|=0x04; }
  if(!(route.humans>=1 && route.humans<=8)) { route.valid=false; route.fail|=0x08; }
  if(!(route.total>=route.humans && route.total<=Scud::MaxCars)) { route.valid=false; route.fail|=0x10; }
  if(route.valid)
  {
    // Validate the owner map against the active cabinets.
    std::array<bool,9> participants{};
    for(unsigned i=0;i<route.humans;++i) {
      const unsigned id=route.players[i];
      if(!id || id>route.machines) { route.valid=false; break; }
      participants[id]=true;
    }
    if(!route.valid || !participants[route.local]) { route.valid=false; route.fail|=0x20; }
    if(route.valid && (route.authority<1 || route.authority>route.machines)) { route.valid=false; route.fail|=0x40; }
    const unsigned stride=addresses?Read32(base+8):0;
    if(route.valid && (stride!=0x300 || base+route.total*stride>0x800000)) { route.valid=false; route.fail|=0x80; }
    if(route.valid) {
      std::array<bool,Scud::MaxCars> seen{};
      for(unsigned i=0;i<route.total;++i) {
        const unsigned object=base+i*stride,id=Read8(object+4);
        if(id>=route.total || seen[id] || Read32(object+8)!=stride || ((i<route.humans)!=(id<route.humans))) { route.valid=false;break; }
        seen[id]=true;
        if(i>=route.humans && (Read32(object)&1)) route.cpu[id]=true;
      }
      if(!route.valid) route.fail|=0x80;
      if(route.valid) {
        const unsigned localId=Read8(base+4);
        if(localId>=route.humans) { route.valid=false; route.fail|=0x80; }
      }
    }
  }
  NetBoard->SetScudRouting(route);
  NetBoard->SetRaceParticipants(route.machines==NetBoard->SyncCabinetCount() &&
    int(route.local)==NetBoard->SyncCabinetIndex()+1 ? GameNetwork::ScudRaceParticipants(
      route.machines,route.local,route.humans,route.players) : 0);
  const unsigned current=Read8(0x104004),next=Read8(0x104005),sub=Read8(0x104006);
  const bool gate=Scud::AtStartGate(current,next,sub);
  const bool entry=current==19 && next==19 && sub==2;
  // Log every stage transition seen by the start barrier.
  static unsigned last=~0u;
  const unsigned tuple=(current<<16)|(next<<8)|sub;
  if(tuple!=last)
  {
    last=tuple;
    InfoLog("%s stage: current=%u next=%u substate=%u%s%s",m_game.name.c_str(),current,next,sub,
      gate?" [start gate]":"",entry?" [left gate]":"");
  }
  return NetBoard->HoldForRaceStart(gate,entry);
}
