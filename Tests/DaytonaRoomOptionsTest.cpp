// SPDX-License-Identifier: GPL-3.0-or-later
// c++ -std=c++17 -Wall -Wextra -Werror -ISrc Tests/DaytonaRoomOptionsTest.cpp -o /tmp/daytona-room-options && /tmp/daytona-room-options
#include "Network/Online/RoomSettings.h"
#include "Network/Games/Daytona2/Collision.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
int main(int argc,char** argv) {
  namespace R=NativeRoomSettings;
  if(argc==2 && std::strcmp(argv[1],"--matrix")==0) {
    for(unsigned game=0;game<9;++game) {
      for(unsigned options=0;options<(1u<<14);++options)
        std::putchar(R::Valid(game,options)?49:48);
      std::putchar(10);
    }
    return 0;
  }
  assert(argc==1);
  for(unsigned game=0;game<=Sgt24hOnline::Game;++game) {
    const bool collision=game<4 || game==8; // Daytona 2 and Scud variants
    const bool pit=game==DaytonaOnline::Game;
    assert(R::SupportsNoCollisions(game)==collision);
    assert(R::SupportsNoPitstop(game)==pit);
    assert(R::HasNoCollisions(game,R::NoCollisions)==collision);
    assert(R::HasNoPitstop(game,R::NoPitstop)==pit);
    assert(!R::HasNoCollisions(game,0) && !R::HasNoPitstop(game,0));
    // Every title except Daytona 1 rejects the pit-stop wire bit, including
    // all Model 2 games and Model 3 variants; Daytona 1 accepts it below.
    if(!pit) assert(!R::Valid(game,R::Defaults(game)|R::NoPitstop));
    if(game<Indy500Online::Game)
      assert(!R::Valid(game,R::Defaults(game)|R::IndyHandicap));
  }
  for(unsigned game:{0u,1u,2u,3u,8u}) {
    const auto base=R::Defaults(game);
    assert(R::Valid(game,base) && R::Valid(game,base|R::NoCollisions));
    assert(R::HasNoCollisions(game,base|R::NoCollisions));
    assert(!R::HasNoPitstop(game,base|R::NoCollisions));
  }
  assert(R::Valid(DaytonaOnline::Game,R::Defaults(DaytonaOnline::Game)|R::NoPitstop));
  assert(!R::SupportsNoCollisions(DaytonaOnline::Game));
  // Verify each Daytona 2 ROM patch independently using only synthetic RAM,
  // including the untouched/disabled and unexpected-instruction guards.
  namespace D=Daytona2Collision;
  for(const auto& site:D::Patches) {
    std::vector<uint8_t> ram(site.address+8,0);
    auto put=[&](uint32_t addr,uint32_t value){std::memcpy(ram.data()+addr,&value,4);};
    put(site.address-4,site.before);put(site.address,site.original);put(site.address+4,site.after);
    assert(D::Apply(site.game,false,ram.data(),ram.size())==D::Result::Disabled);
    assert(D::Read(ram.data()+site.address)==site.original);
    assert(D::Apply(site.game,true,ram.data(),ram.size())==D::Result::Applied);
    assert(D::Read(ram.data()+site.address)==D::ReplacementWord);
    assert(D::Apply(site.game,true,ram.data(),ram.size())==D::Result::AlreadyApplied);
    put(site.address-4,0xdeadbeefu);
    assert(D::Apply(site.game,true,ram.data(),ram.size())==D::Result::UnexpectedInstruction);
  }
  std::puts("PASS game-specific room controls, relay-compatible masks and Daytona 2 collision patch guards");
}
