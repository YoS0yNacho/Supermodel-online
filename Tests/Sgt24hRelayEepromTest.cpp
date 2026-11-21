#include "Network/Games/Sgt24h/RelayEeprom.h"
#include <cassert>
using namespace Sgt24h;
EepromImage fixture() {
 EepromImage a{};a.fill(0x7654);
 a[0]=0x3353;a[1]=0x4132;a[2]=0x4906;a[3]=0;
 for(unsigned r:{PrimarySettings,BackupSettings}) {
  for(unsigned i=1;i<SettingsWords;++i)a[r+i]=uint16_t(i*0x102);
  a[r+4]=0xad85;a[r]=SettingsChecksum(a,r);
 }
 return a;
}
int main() {
 // Independent known checksum: 15 words of 0x0102 sum to 45.
 EepromImage sum{};for(unsigned i=5;i<20;++i)sum[i]=0x0102;
 assert(SettingsChecksum(sum,4)==45);
 for(unsigned n=2;n<=4;++n)for(unsigned seat=0;seat<n;++seat) {
  auto a=fixture(),old=a;assert(ConfigureRelayEeprom(a,seat,n));
  for(unsigned r:{PrimarySettings,BackupSettings}) {
   assert(ValidSettings(a,r));assert((a[r+8]>>8)==n);
   assert(a[r+9]==(seat==0?0x100:0));assert((a[r+10]&255)==seat);
   assert((a[r+8]&255)==(old[r+8]&255));
   assert((a[r+10]&0xff00)==(old[r+10]&0xff00));
   for(unsigned i=1;i<16;++i)if(i!=8&&i!=9&&i!=10)assert(a[r+i]==old[r+i]);
  }
  for(unsigned i=0;i<64;++i)if(i<4||i>=36)assert(a[i]==old[i]);
  auto stable=a;assert(ConfigureRelayEeprom(a,seat,n)&&a==stable);
 }
 for(unsigned n:{0u,1u,5u,99u}){auto a=fixture(),old=a;assert(!ConfigureRelayEeprom(a,0,n)&&a==old);}
 for(unsigned seat:{2u,4u,99u}){auto a=fixture(),old=a;assert(!ConfigureRelayEeprom(a,seat,2)&&a==old);}
 for(unsigned i=0;i<4;++i){auto a=fixture();a[i]^=1;auto old=a;assert(!ConfigureRelayEeprom(a,0,2)&&a==old);}
 auto a=fixture();a[4]^=1;assert(ConfigureRelayEeprom(a,1,4));assert(ValidSettings(a,4)&&ValidSettings(a,20));
 a=fixture();a[20]^=1;assert(ConfigureRelayEeprom(a,0,2));assert(ValidSettings(a,20));
 a=fixture();a[4]^=1;a[20]^=1;auto old=a;assert(!ConfigureRelayEeprom(a,0,2)&&a==old);
 a=fixture();a[8]=a[24]=0; a[4]=SettingsChecksum(a,4);a[20]=SettingsChecksum(a,20);
 old=a;assert(!ConfigureRelayEeprom(a,0,2)&&a==old);
}
