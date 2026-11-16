// Exact native EEPROM service settings, applied to the online image only.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_NATIVE_SESSION_SETTINGS_H
#define INCLUDED_NATIVE_SESSION_SETTINGS_H
#include "Network/Online/RoomSettings.h"
#include "Daytona2/Daytona2Relay.h"
#include "Scud/Policy.h"
#include "Srally2/Policy.h"
namespace NativeSessionSettings {
// `research` selects the optional research entry of a ROM (Scud Race Plus
// 16-player, lobby game 8): its room validates car numbers 1..16, the stock
// entry 1..8. The ROM name alone cannot tell the two apart.
inline bool Apply(std::array<uint16_t,64>& image,const std::string& game,uint32_t options,unsigned carNumber,bool research=false){
 unsigned id=9;
 const char* names[]={"daytona2","dayto2pe","scud","scudplus","srally2","lemans24","srally2_8p","dirtdvls"};
 for(unsigned i=0;i<8;++i)if(game==names[i])id=i;
 if(research && id==3)id=8; // Scud Race Plus - 16 Player
 if(!NativeRoomSettings::Valid(id,options) || (carNumber && carNumber>NativeRoomSettings::CarLimit(id)))return false;
 // Le Mans 24 and Dirt Devils have no native difficulty/mode/car settings:
 // only the emulator-side Video60 room option may be set (it never touches the
 // EEPROM; the native MASTER/SLAVE assignment is done by ConfigureRelayEeprom).
 if(id==5 || id==7)return !(options&~NativeRoomSettings::Video60) && carNumber==0;
 if(image[0]!=0x4d33 || image[1]!=0x5345 || image[2]!=0x4741)return false;
 if(id==4){
  if(image[5]!=Srally2::SettingsCRC(image))return false;
  if(options&NativeRoomSettings::Enabled){
   uint32_t word=(uint32_t(image[12])<<16)|image[13];
   word=(word&~((7u<<19)|(1u<<5)))|(NativeRoomSettings::Difficulty(options)<<19)|(NativeRoomSettings::Mode(options)<<5);
   image[12]=uint16_t(word>>16);image[13]=uint16_t(word);
   image[5]=Srally2::SettingsCRC(image);
  }
 }else{
  if(image[3]!=(id<2?Dayto2pe::SettingsCRC(image):Scud::SettingsCRC(image)))return false;
  if(carNumber){image[15]=uint16_t((image[15]&0xff00)|(carNumber-1));image[44]=image[15];}
  if(options&NativeRoomSettings::Enabled){
   image[16]=uint16_t((image[16]&0xff00)|NativeRoomSettings::Difficulty(options));
   image[17]=uint16_t((image[17]&0xff00)|NativeRoomSettings::Mode(options));
   image[45]=image[16];image[46]=image[17];
  }
  image[3]=id<2?Dayto2pe::SettingsCRC(image):Scud::SettingsCRC(image);
 }
 return true;
}
}
#endif
