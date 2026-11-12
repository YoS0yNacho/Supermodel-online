// Scud / Scud Plus: session-only "real players only" patch.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_SCUD_RACE_OPTIONS_H
#define INCLUDED_SCUD_RACE_OPTIONS_H
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace ScudRaceOptions {
// Native behaviour (verified on owner's ROMs with the real-ROM harness):
// holding Start at transmission selection sets the mode word at RAM
// 0x1051A0 (SDA -0x2E60 with r13=0x108000) to 1: "REAL RACERS ONLY". The
// game then builds a grid with no CPU cars (0x104018 total cars 40 -> 1,
// 0x10401C CPU count 39 -> 0); a solo cabinet becomes a time trial. The
// setters test the latched Start bit from input word 0x100080. NOP-ing the
// transmission setter's "Start not held" branch makes it store 1 exactly
// as if Start were held, on every cabinet that applies it. The twin
// setter for the other selection screen (Scud Plus flag 0x1051A4) and the
// Scud Plus Secret Cars gate at 0x2BB90 are deliberately untouched.
inline constexpr uint32_t EnabledWord=0x60000000;
struct Patch { const char* game; uint32_t address, original, before, after; };
inline constexpr Patch Patches[]={
 // scud: inline transmission setter at 0x299E8; store at 0x29A0C.
 {"scud",0x000299f4,0x41820018,0x5520d801,0x48000010},
 // scudplus: factored setter 0x2BB58; store at 0x2BB78.
 {"scudplus",0x0002bb6c,0x41820008,0x5480d801,0x38600001},
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
