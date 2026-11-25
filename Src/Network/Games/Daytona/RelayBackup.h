// Original Model 2 Daytona USA Rev A: in-memory operator settings for a relay session.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_DAYTONA_RELAY_BACKUP_H
#define INCLUDED_DAYTONA_RELAY_BACKUP_H
#include "Network/Online/RoomSettings.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
namespace DaytonaOnline {
inline constexpr unsigned BlockBytes=0x80, IoOffset=0x100;
inline constexpr std::array<uint8_t,8> Signature{{'S','E','G','A',0x40,0x82,0x02,0x00}};
inline uint16_t BackupChecksum(const uint8_t* block)
{
  uint16_t v=0;
  for (unsigned i=10;i<BlockBytes;++i)
  {
    v^=uint16_t(block[i])<<8;
    for (unsigned bit=0;bit<8;++bit) v=uint16_t((unsigned(v)<<1) ^ ((v&0x8000)?0x1021:0));
  }
  return v;
}
inline bool ValidBlock(const uint8_t* block)
{
  return std::equal(Signature.begin(),Signature.end(),block) &&
    BackupChecksum(block)==(uint16_t(block[8])|(uint16_t(block[9])<<8));
}
inline bool ConfigureRelayBackup(uint8_t* ram,size_t bytes,uint8_t* dpram,size_t ioBytes,
                                 unsigned seat,unsigned carNumber,uint32_t options)
{
  if (!ram || bytes<2*BlockBytes || !dpram || ioBytes<IoOffset+BlockBytes ||
      !seat || seat>Cabinets || !NativeRoomSettings::Valid(Game,options)) return false;
  auto* io=dpram+IoOffset;
  // The ROM also loads an operator-settings copy from the Model 1 I/O
  // board's DPRAM mailbox. Leaving it stale can put a guest in MASTER.
  const uint8_t* valid=ValidBlock(io)?io:ValidBlock(ram)?ram:ValidBlock(ram+BlockBytes)?ram+BlockBytes:nullptr;
  if (!valid) return false;
  std::array<uint8_t,BlockBytes> edited{};
  std::copy_n(valid,BlockBytes,edited.data());
  // Only Game System fields. In particular +0x7c changes on a service
  // commit even when CAR NUMBER does not; it is not a duplicate seat.
  edited[0x0b]=uint8_t(seat);
  edited[0x0c]=seat==1?0:1;
  edited[0x1c]=uint8_t(NativeRoomSettings::Mode(options));
  edited[0x20]=uint8_t(NativeRoomSettings::Difficulty(options));
  if(carNumber)edited[0x7c]=uint8_t(carNumber);
  const auto sum=BackupChecksum(edited.data());
  edited[8]=uint8_t(sum);edited[9]=uint8_t(sum>>8);
  std::copy(edited.begin(),edited.end(),ram);
  std::copy(edited.begin(),edited.end(),ram+BlockBytes);
  std::copy(edited.begin(),edited.end(),io);
  return true;
}
}
#endif
