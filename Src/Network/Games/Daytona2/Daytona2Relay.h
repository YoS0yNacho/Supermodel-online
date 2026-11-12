// Session-only native exact-Daytona cabinet configuration. GPL-3.0-or-later.
#ifndef INCLUDED_DAYTONA2RELAY_H
#define INCLUDED_DAYTONA2RELAY_H
#include <array>
#include <cstdint>
#include "Policy.h"
namespace Dayto2pe {
using EepromImage = std::array<uint16_t,64>;
// Native CRC-16/CCITT, initial zero, over the 29 settings words, MSB first.
inline uint16_t SettingsCRC(const EepromImage& image) {
  uint16_t crc=0;
  for(unsigned i=6;i<35;++i) for(unsigned shift : {8u,0u}) {
    crc ^= uint16_t((image[i]>>shift)&255)<<8;
    for(unsigned bit=0;bit<8;++bit) crc=uint16_t((crc<<1)^((crc&0x8000)?0x1021:0));
  }
  return crc;
}
inline bool ConfigureRelayEeprom(EepromImage& image,unsigned index,const std::string& game="dayto2pe",bool spectate=false) {
  const auto* profile=GetProfile(game);
  if(!profile || index>=MaxCabinets || image[0]!=0x4d33 || image[1]!=0x5345 || image[2]!=0x4741 ||
     image[4]!=0xa210 || image[5]!=profile->eepromRevision || image[3]!=SettingsCRC(image)) return false;
  // EEPROM word 15 stores role in the high byte and car index in the low byte.
  const unsigned role = (index == 0) ? 1 : (spectate ? 3 : 2);
  image[6]=uint16_t((image[6]&0xff00)|role);
  image[15]=uint16_t((role<<8)|index);
  image[35]=image[6]; image[44]=image[15]; image[3]=SettingsCRC(image);
  return true;
}
}
#endif
