// Super GT 24h native settings. Pure image transform; no persistence or launch hook.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_SGT24H_RELAY_EEPROM_H
#define INCLUDED_SGT24H_RELAY_EEPROM_H
#include "Profile.h"
#include <array>
namespace Sgt24h {
using EepromImage=std::array<uint16_t,64>;
constexpr unsigned PrimarySettings=4, BackupSettings=20, SettingsWords=16;
// 0x71a18..0x71b8c: sum 30 bytes after each checksum, NOT CRC-CCITT.
inline uint16_t SettingsChecksum(const EepromImage& image,unsigned record) {
 if(record!=PrimarySettings && record!=BackupSettings)return 0;
 unsigned sum=0;
 for(unsigned i=1;i<SettingsWords;++i)sum+=(image[record+i]&255)+(image[record+i]>>8);
 return uint16_t(sum);
}
inline bool ValidSettings(const EepromImage& image,unsigned record) {
 return (record==PrimarySettings || record==BackupSettings) &&
        image[record+4]==0xad85 && image[record]==SettingsChecksum(image,record);
}
// Caller must gate on the verified program and suppress disk NVRAM writes
// for the entire online session. This helper alone does not enforce that.
inline bool ConfigureRelayEeprom(EepromImage& image,unsigned seat,unsigned count) {
 if(count<2 || count>Cabinets || seat>=count || image[0]!=0x3353 ||
    image[1]!=0x4132 || image[2]!=0x4906 || image[3]!=0)return false;
 const unsigned source=ValidSettings(image,PrimarySettings)?PrimarySettings:BackupSettings;
 if(!ValidSettings(image,source))return false;
 auto next=image;
 // Relative to 0x5109a8: count +0x11, disabled +0x12,
 // master +0x13, zero-based seat +0x14. Service save: 0xbc51c.
 next[source+8]=uint16_t((next[source+8]&0xff)|(count<<8));
 next[source+9]=seat==0?0x0100:0x0000;
 next[source+10]=uint16_t((next[source+10]&0xff00)|seat);
 const unsigned other=source==PrimarySettings?BackupSettings:PrimarySettings;
 for(unsigned i=1;i<SettingsWords;++i)next[other+i]=next[source+i];
 next[PrimarySettings]=next[BackupSettings]=SettingsChecksum(next,source);
 image=next;
 return true;
}
}
#endif
