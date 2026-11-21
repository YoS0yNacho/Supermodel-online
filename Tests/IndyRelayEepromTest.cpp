// SPDX-License-Identifier: GPL-3.0-or-later
#include "Network/Games/Indy500/RelayEeprom.h"
#include <cassert>
#include <cstdio>

using namespace Indy500Online;

int main()
{
  // Rev A native Standalone settings record, with known checksum 0x0f3f.
  const std::array<uint16_t,SettingsWords> settings = {
    0x0000,0x0065,0x0000,0x020b,0x0101,0x0000,0x0001,0x0200,0x0001,
    0x0302,0x0000,0x4000,0xc080,0x8040,0x40c0,0xc080,0x0080
  };
  EepromImage base;
  base.fill(0xffff);
  base[0]=0x3353; base[1]=0x4132; base[2]=0x0907; base[3]=0;
  for (unsigned copy : {PrimarySettings,BackupSettings})
  {
    for (unsigned n=0; n<settings.size(); ++n) base[copy+1+n]=settings[n];
    base[copy]=0x0f3f;
    assert(SettingsChecksum(base,copy)==base[copy]);
  }
  for (unsigned index=0; index<Cabinets; ++index)
  {
    auto image=base;
    assert(ConfigureRelayEeprom(image,index));
    for (unsigned copy : {PrimarySettings,BackupSettings})
    {
      assert(image[copy]==SettingsChecksum(image,copy));
      assert(image[copy+1+RoleAndIdWord]==((index<<8)|(index ? 2u : 1u)));
    }
    for (unsigned n=0; n<image.size(); ++n)
      if (n!=PrimarySettings && n!=BackupSettings && n!=PrimarySettings+1+RoleAndIdWord && n!=BackupSettings+1+RoleAndIdWord)
        assert(image[n]==base[n]);
    auto at60=base;
    assert(ConfigureRelayEeprom(at60,index,NativeRoomSettings::Video60) && at60==image);
    const auto same=image;
    assert(ConfigureRelayEeprom(image,index) && image==same);
  }
  // All 20 shared combinations are native encodings. Keep high/unrelated
  // bits of both words, the other settings, and the image tail intact.
  namespace Room = NativeRoomSettings;
  for (unsigned diff=0; diff<5; ++diff)
    for (unsigned mode=0; mode<2; ++mode)
      for (unsigned handicap=0; handicap<2; ++handicap)
      {
        auto image=base;
        for (unsigned copy : {PrimarySettings,BackupSettings})
        {
          image[copy+1+ModeDifficultyWord]|=0xa0f0;
          image[copy+1+HandicapWord]|=0x40c0;
          image[copy]=SettingsChecksum(image,copy);
        }
        const auto before=image;
        const uint32_t options=Room::Enabled | (diff<<2) | (mode<<5) | (handicap?Room::IndyHandicap:0u);
        assert(ConfigureRelayEeprom(image,1,options));
        auto expected=before;
        for (unsigned copy : {PrimarySettings,BackupSettings})
        {
          expected[copy+1+RoleAndIdWord]=0x0102;
          expected[copy+1+ModeDifficultyWord]=uint16_t(0xa0f0u | (diff<<8) | mode);
          expected[copy+1+HandicapWord]=uint16_t(0x40c0u | handicap);
          expected[copy]=SettingsChecksum(expected,copy);
        }
        assert(image==expected);
        auto at60=before;
        assert(ConfigureRelayEeprom(at60,1,options | Room::Video60) && at60==expected);
      }
  assert(Room::Defaults(Game)==(2u|(2u<<2)|(1u<<11)));
  for (uint32_t invalid : {Room::NoCollisions, Room::IndyHandicap, Room::Enabled|(5u<<2), Room::Enabled|(2u<<5)})
  {
    auto image=base;
    assert(!ConfigureRelayEeprom(image,0,invalid) && image==base);
  }
  auto damaged=base;
  damaged[PrimarySettings+1]^=0x1000;
  assert(ConfigureRelayEeprom(damaged,7));
  auto restored=base;
  assert(ConfigureRelayEeprom(restored,7) && restored==damaged);
  damaged=base;
  damaged[BackupSettings+3]^=0x4000;
  damaged[BackupSettings]=SettingsChecksum(damaged,BackupSettings);
  assert(ConfigureRelayEeprom(damaged,7) && restored==damaged);
  damaged=base;
  damaged[PrimarySettings]^=1; damaged[BackupSettings]^=1;
  auto untouched=damaged;
  assert(!ConfigureRelayEeprom(damaged,0) && damaged==untouched);
  damaged=base;
  assert(!ConfigureRelayEeprom(damaged,Cabinets) && damaged==base);
  damaged[2]^=1;
  untouched=damaged;
  assert(!ConfigureRelayEeprom(damaged,0) && damaged==untouched);
  damaged.fill(0xffff);
  untouched=damaged;
  assert(!ConfigureRelayEeprom(damaged,1) && damaged==untouched);
  std::puts("PASS INDY Rev A EEPROM: eight native roles, 20 room settings combinations, masked fields, both checksums, mirrored recovery and rejection");
}
