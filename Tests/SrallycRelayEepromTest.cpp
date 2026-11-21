// Sega Rally Championship Revision C settings contract, without a ROM.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Network/Games/Srallyc/RelayEeprom.h"
#include <cassert>
#include <cstring>
#include <iostream>

int main()
{
  using namespace SrallycOnline;
  // Service-menu dumps from disposable Rev C instances, with unused words
  // omitted. Stock and menu-committed images differ outside game settings.
  const EepromImage stock = {0xeada,0x0024,1,0,1,1,0,0x0401,0,0x3f80,
                             0xfff2,0xffff,1,2,0,0x000b,1,1};
  const EepromImage committed = {0x8cc7,0x0024,2,0,1,0x0101,0,0x0401,0x999a,0x3f99,
                                 0xfff2,0xffff,1,2,0,0x000b,1,1};
  assert(SettingsChecksum(stock)==stock[0] && SettingsChecksum(committed)==committed[0]);
  constexpr uint16_t nativeSeatChecksum[] = {0x8cc7,0x421b,0x07af,0xcf82};
  for(unsigned i=0; i<Cabinets; ++i)
  {
    auto copy=committed;
    assert(ConfigureRelayEeprom(copy,i+1,NativeRoomSettings::Defaults(Game)));
    assert(copy[0]==nativeSeatChecksum[i] && (copy[RoleWord]>>8)==i+1 && copy[DifficultyModeWord]==0);
  }
  // Reproduce native checksum on each actually written setting (NOTLINK in the
  // source image only; this is not an allowed relay role).
  auto bytes=committed;
  bytes[RoleWord]&=0xff;
  constexpr uint16_t difficultyCrc[] = {0,0xfe70,0xa775,0x9076};
  for(unsigned i=1; i<=3; ++i) { bytes[DifficultyModeWord]=i; assert(SettingsChecksum(bytes)==difficultyCrc[i]); }
  bytes[DifficultyModeWord]=0x0100; assert(SettingsChecksum(bytes)==0xac78);
  bytes[DifficultyModeWord]=0x0200; assert(SettingsChecksum(bytes)==0x0365);
  constexpr unsigned map[]={1,0,2,3};
  for(unsigned seat=1;seat<=Cabinets;++seat)
    for(unsigned d=0;d<4;++d)
      for(unsigned m=0;m<3;++m)
      {
        auto copy=stock;
        const uint32_t options=2 | (d<<2) | (m<<5);
        assert(ConfigureRelayEeprom(copy,seat,options));
        auto at60=stock;
        assert(ConfigureRelayEeprom(at60,seat,options | NativeRoomSettings::Video60) && at60==copy);
        assert(copy[0]==SettingsChecksum(copy));
        for (unsigned j=1; j<copy.size(); ++j)
          assert(copy[j]==(j==RoleWord?((seat<<8)|(stock[j]&0xffu)):j==DifficultyModeWord?((m<<8)|map[d]):stock[j]));
      }
  for(auto invalid : {0u,1u,2u|(4u<<2),2u|(3u<<5),2u|256u,2u|512u,2u|2048u,2u|0x80000000u})
  {
    auto copy=stock;
    assert(!ConfigureRelayEeprom(copy,1,invalid) && copy==stock);
  }
  auto copy=stock;
  assert(!ConfigureRelayEeprom(copy,0,6) && !ConfigureRelayEeprom(copy,5,6) && copy==stock);
  copy[RoleWord]^=1u;
  const auto bad=copy;
  assert(!ConfigureRelayEeprom(copy,1,6) && copy==bad);
  copy.fill(0xffff);
  assert(!ConfigureRelayEeprom(copy,1,6));
  assert(std::strcmp(NativeRoomSettings::DifficultyName(Game,1),"Normal")==0);
  assert(std::strcmp(NativeRoomSettings::ModeName(Game,2),"Long")==0);
  std::cout << "PASS Rev C native menu checksums, 4 roles x 12 settings, exact writes and invalid-input rejection\n";
}
