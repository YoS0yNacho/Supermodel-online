// Daytona USA 2 / Power Edition: session-only "real players only" patch.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_DAYTONA2_RACE_OPTIONS_H
#define INCLUDED_DAYTONA2_RACE_OPTIONS_H
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace Daytona2RaceOptions {
// Exact-ROM setter patch enables the native real-players-only selection.
inline constexpr uint32_t EnabledWord=0x60000000;
struct Patch { const char* game; uint32_t address, original, before, after; };
inline constexpr Patch Patches[]={
 // daytona2: setter 0x39C60, called from transmission leaf 0x34A70.
 {"daytona2",0x00039c78,0x41820008,0x5480d801,0x38600001},
 // dayto2pe: setter 0xB0910 (input word 0x7377EC), same flag 0x106200.
 {"dayto2pe",0x000b0920,0x41820008,0x5480d801,0x38600001},
};
enum class Result { Disabled, WrongGame, NotReady, UnexpectedInstruction, Applied, AlreadyApplied };
inline uint32_t Read(const uint8_t* p) { uint32_t v;std::memcpy(&v,p,sizeof(v));return v; }
inline Result Apply(const char* game,bool enabled,uint8_t* ram,size_t size) {
  if(!enabled)return Result::Disabled;
  const Patch* patch=nullptr;
  if(game)for(const auto& p:Patches)if(!std::strcmp(game,p.game))patch=&p;
  if(!patch)return Result::WrongGame;
  if(!ram || size<patch->address+8)return Result::NotReady;
  const uint32_t before=Read(ram+patch->address-4),word=Read(ram+patch->address),after=Read(ram+patch->address+4);
  if(!before && !word && !after)return Result::NotReady;
  // Refuse other revisions or an incompletely copied code image.
  if(before!=patch->before || after!=patch->after)return Result::UnexpectedInstruction;
  if(word==EnabledWord)return Result::AlreadyApplied;
  if(word!=patch->original)return Result::UnexpectedInstruction;
  const uint32_t nop=EnabledWord;std::memcpy(ram+patch->address,&nop,sizeof(nop));return Result::Applied;
}
}
#endif
