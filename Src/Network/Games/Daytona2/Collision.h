// Daytona USA 2 / Power Edition: session-only "no car collisions" patch.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_DAYTONA2_COLLISION_H
#define INCLUDED_DAYTONA2_COLLISION_H
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace Daytona2Collision {
// Exact-ROM patch skips only car-to-car response; course and wall physics remain native.
inline constexpr uint32_t ReplacementWord=0x4e800020; // blr
struct Patch { const char* game; uint32_t address, original, before, after; };
inline constexpr Patch Patches[]={
 {"daytona2",0x0002bedc,0x3821fff0,0x4e800020,0x7c0802a6},
 {"dayto2pe",0x00030b34,0x3821fff0,0x4e800020,0x7c0802a6},
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
  if(word==ReplacementWord)return Result::AlreadyApplied;
  if(word!=patch->original)return Result::UnexpectedInstruction;
  const uint32_t w=ReplacementWord;std::memcpy(ram+patch->address,&w,sizeof(w));return Result::Applied;
}
}
#endif
