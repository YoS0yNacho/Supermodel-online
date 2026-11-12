// Scud / Scud Plus: session-only "no car collisions" patch.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_SCUD_COLLISION_H
#define INCLUDED_SCUD_COLLISION_H
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace ScudCollision {
// Native behaviour (verified on owner's ROMs with the real-ROM pin oracle):
// the pairwise loop calls the contact-response function (scud 0x22990,
// scudplus 0x24BB0) for each close pair; only when it reports contact does
// the loop run the velocity integrator, position separation and momentum
// response. Replacing that single call with a branch to the loop's next
// iteration makes all cars ghost through each other while wall/course
// physics, AI and timers stay original. Proven: with a CPU car pinned
// 3.0 units ahead of the player, the player's per-frame step stays at the
// free-running value (scud 1.231 vs 0.543 unpatched; scudplus 1.230 vs
// 0.589) on both games.
inline constexpr uint32_t ReplacementWord=0x48000014; // b -> next pair iteration
struct Patch { const char* game; uint32_t address, original, before, after; };
inline constexpr Patch Patches[]={
 {"scud",0x00022804,0x4800018d,0x40800018,0x41820010},
 {"scudplus",0x00024a24,0x4800018d,0x40800018,0x41820010},
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
