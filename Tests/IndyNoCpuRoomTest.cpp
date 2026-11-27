// SPDX-License-Identifier: GPL-3.0-or-later
#include "Network/Games/Indy500/RelayEeprom.h"
#include <cassert>
#include <cstdio>
int main() {
  namespace R=NativeRoomSettings;
  constexpr unsigned indy=Indy500Online::Game;
  assert(!R::HasRealPlayers(indy,R::Defaults(indy)));
  assert(!R::Valid(indy,R::RealPlayers)); // explicit shared settings required
  assert(!R::HasNoCollisions(indy,R::NoCollisions));
  assert(!R::Valid(indy,R::Defaults(indy)|R::NoCollisions));
  assert(!R::Valid(SrallycOnline::Game,R::Defaults(SrallycOnline::Game)|R::RealPlayers));
  Indy500Online::EepromImage base; base.fill(0xffff);
  base[0]=0x3353;base[1]=0x4132;base[2]=0x0907;base[3]=0;
  const std::array<uint16_t,Indy500Online::SettingsWords> settings={
    0,0x0065,0,0x020b,0x0101,0,1,0x0200,1,0x0302,0,0x4000,0xc080,0x8040,0x40c0,0xc080,0x0080};
  for(unsigned copy:{Indy500Online::PrimarySettings,Indy500Online::BackupSettings}) {
    for(unsigned i=0;i<settings.size();++i)base[copy+1+i]=settings[i];
    base[copy]=Indy500Online::SettingsChecksum(base,copy);
  }
  for(unsigned seat=0;seat<8;++seat)
    for(unsigned diff=0;diff<5;++diff)
      for(unsigned mode=0;mode<2;++mode)
        for(unsigned handicap=0;handicap<2;++handicap)
          for(unsigned video=0;video<2;++video) {
            const uint32_t rules=R::Enabled|(diff<<2)|(mode<<5)|(handicap?R::IndyHandicap:0)|(video?R::Video60:0);
            assert(R::Valid(indy,rules) && R::Valid(indy,rules|R::RealPlayers));
            assert(R::HasRealPlayers(indy,rules|R::RealPlayers));
            auto ordinary=base, playerOnly=base;
            assert(Indy500Online::ConfigureRelayEeprom(ordinary,seat,rules));
            assert(Indy500Online::ConfigureRelayEeprom(playerOnly,seat,rules|R::RealPlayers));
            assert(ordinary==playerOnly); // game patch option is never EEPROM state
          }
  std::puts("PASS Indy players-only room mask, defaults, game isolation and in-memory EEPROM invariance");
}
