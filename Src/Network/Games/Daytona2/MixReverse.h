// Daytona USA 2 Power Edition: Reverse mode on the Mix (Challenge) course.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_DAYTONA2_MIX_REVERSE_H
#define INCLUDED_DAYTONA2_MIX_REVERSE_H
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace Daytona2MixReverse {
// The car-select leaf at 0xAF358 latches Reverse (word 0x106214) from the brake:
//   0x106214 = 0
//   if (race mode byte 0x1001A3 != 0) return;      // GP / Endurance: no Reverse
//   if (course byte 0x105FC4 == 0) return;         // course 0 is MIX: no Reverse  <- removed
//   if (brake word 0x737C0C < 0x1F) return;
//   0x106214 = 1
// Mirror (0x106204, Start at course confirm) already works on MIX; only this branch keeps the
// brake from arming Reverse there. The patch NOPs it, so MIX accepts Reverse and Reverse+Mirror
// exactly like the three regular courses. Always on, not a room option.
inline constexpr uint32_t Address=0x000af38c, Original=0x41820020, Patched=0x60000000;
inline constexpr uint32_t Context[]={0x3c600010,0x88635fc4,0x2c030000};  // lis r3,0x10 | lbz r3,0x5fc4(r3) | cmpwi r3,0
inline constexpr uint32_t After[]={0x3c600073,0x80637c0c,0x2c03001f,0x41800010,0x38600001,0x3c400010,0x90626214};
enum class Result { WrongGame, NotReady, UnexpectedInstruction, Applied, AlreadyApplied };
inline uint32_t Read(const uint8_t* p) { uint32_t v;std::memcpy(&v,p,sizeof(v));return v; }
inline Result Apply(const char* game,uint8_t* ram,size_t size) {
  if(!game||std::strcmp(game,"dayto2pe"))return Result::WrongGame;
  constexpr size_t ctx=sizeof(Context)/4,aft=sizeof(After)/4;
  if(!ram||size<Address+4+aft*4)return Result::NotReady;
  const uint32_t word=Read(ram+Address);
  // The program is copied from CROM into RAM at boot; wait until the leaf is there.
  if(!word&&!Read(ram+Address-4))return Result::NotReady;
  for(size_t i=0;i<ctx;++i)if(Read(ram+Address-ctx*4+i*4)!=Context[i])return Result::UnexpectedInstruction;
  for(size_t i=0;i<aft;++i)if(Read(ram+Address+4+i*4)!=After[i])return Result::UnexpectedInstruction;
  if(word==Patched)return Result::AlreadyApplied;
  if(word!=Original)return Result::UnexpectedInstruction;
  const uint32_t nop=Patched;std::memcpy(ram+Address,&nop,sizeof(nop));return Result::Applied;
}
}
#endif
