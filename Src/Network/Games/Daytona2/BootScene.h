// Daytona USA 2 Power Edition: session-only boot-scene patch.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_DAYTONA2_BOOT_SCENE_H
#define INCLUDED_DAYTONA2_BOOT_SCENE_H
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace Daytona2BootScene {
// Why: the boot program loads a canned Real3D database from CROM bank 5 (header at
// 0xFF400000: about 13 000 culling nodes and 5 000 models) and its first entry is a
// viewport that points at all of it. That viewport is DMA'd to culling RAM 0x8E000000
// and stays submitted for the whole NETWORK CHECKING phase, hidden behind the opaque
// 2D text layer. Nothing of it is visible, but it costs hundreds of milliseconds per
// frame to draw, and the link check advances one exchange per frame. daytona2 submits
// an empty viewport in the same phase.
//
// The database loader (0x135D4, one caller: boot init 0x189B0) walks the header and
// for the viewport entry calls the hook at 0x13AE0 (one caller, 0x13D4C), which records
// the viewport's root pointer and LOD table address in RAM (0x737918/0x737928) so that
// later entries can be copied into the game's own tables (0x711278). The caller keeps
// the entry's culling offset in r31 and, after the hook returns, builds the DMA target
// as `oris r3, r31, 0x8e00`. The hook saves and restores r31 without using it.
//
// Patch: restore r31 with `li r31, 0x7f00` instead of `lwz r31, 0x1c(r1)`. The viewport
// template then lands at 0x8E007F00, a hole between the boot entries (0x6740 is the end
// of the pointer list, 0x8000 the next entry), and culling RAM 0x800000 keeps the zeros
// it has after reset: an empty frame, exactly what daytona2 shows there. The hook's
// bookkeeping is untouched, so the pointer-list copy the game uses later is byte for byte
// the same. The game writes its own viewport from RAM (0x660010) once the check passes.
inline constexpr uint32_t Address=0x00013b34;
inline constexpr uint32_t Original=0x83e1001c;   // lwz r31, 0x1c(r1)
inline constexpr uint32_t Patched=0x3be07f00;    // li  r31, 0x7f00
inline constexpr uint32_t Before=0x83c10018;     // lwz r30, 0x18(r1)
inline constexpr uint32_t After=0x38210020;      // addi r1, r1, 0x20
// Context checked so other revisions or a half-copied image are refused.
inline constexpr uint32_t HookEntry=0x00013ae0, HookEntryWord=0x9421ffe0;  // stwu r1, -0x20(r1)
inline constexpr uint32_t HookCall=0x00013d4c, HookCallWord=0x4bfffd95;    // bl 0x13ae0
inline constexpr uint32_t DestBuild=0x00013d88, DestBuildWord=0x67e38e00; // oris r3, r31, 0x8e00
enum class Result { Disabled, WrongGame, NotReady, UnexpectedInstruction, Applied, AlreadyApplied };
// Emulator RAM is host-endian 32-bit words, NOT a big-endian ROM byte stream.
inline uint32_t Read(const uint8_t* p) { uint32_t v;std::memcpy(&v,p,sizeof(v));return v; }
inline Result Apply(const char* game,bool enabled,uint8_t* ram,size_t size) {
  if(!enabled)return Result::Disabled;
  if(!game || std::strcmp(game,"dayto2pe"))return Result::WrongGame;
  if(!ram || size<DestBuild+8)return Result::NotReady;
  const uint32_t before=Read(ram+Address-4),word=Read(ram+Address),after=Read(ram+Address+4);
  if(!before && !word && !after)return Result::NotReady;
  if(before!=Before || after!=After || Read(ram+HookEntry)!=HookEntryWord ||
     Read(ram+HookCall)!=HookCallWord || Read(ram+DestBuild)!=DestBuildWord)return Result::UnexpectedInstruction;
  if(word==Patched)return Result::AlreadyApplied;
  if(word!=Original)return Result::UnexpectedInstruction;
  std::memcpy(ram+Address,&Patched,sizeof(Patched));return Result::Applied;
}
}
#endif
