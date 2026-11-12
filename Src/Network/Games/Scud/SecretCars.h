// Scud Race Plus Rev A: session-only native secret-car gate patch.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_SCUDPLUS_SECRET_CARS_H
#define INCLUDED_SCUDPLUS_SECRET_CARS_H
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace ScudPlusSecretCars {
inline constexpr uint32_t Address=0x0002bb90, EnabledWord=0x60000000;
// Verified from the owner's exact Export Rev A EPRs: source FFF3BB90.
// Native boot FFF01538..FFF01548 copies FFF10000 -> RAM 0 (0x28000 words).
inline constexpr uint32_t OriginalWord=0x4082001c;
enum class Result { Disabled, WrongGame, NotReady, UnexpectedInstruction, Applied, AlreadyApplied };
// Emulator RAM is host-endian 32-bit words, NOT a big-endian ROM byte stream.
inline uint32_t Read(const uint8_t* p) { uint32_t v;std::memcpy(&v,p,sizeof(v));return v; }
inline Result Apply(const char* game,bool enabled,uint8_t* ram,size_t size) {
  if(!enabled)return Result::Disabled;
  if(!game || std::strcmp(game,"scudplus"))return Result::WrongGame;
  if(!ram || size<Address+8)return Result::NotReady;
  const uint32_t before=Read(ram+Address-4),word=Read(ram+Address),after=Read(ram+Address+4);
  if(!before && !word && !after)return Result::NotReady;
  // Refuse other revisions/unexpected or incompletely copied code.
  if(before!=0x2c030004 || after!=0x806d8080)return Result::UnexpectedInstruction;
  if(word==EnabledWord)return Result::AlreadyApplied;
  if(word!=OriginalWord)return Result::UnexpectedInstruction;
  const uint32_t nop=EnabledWord;std::memcpy(ram+Address,&nop,sizeof(nop));return Result::Applied;
}
}
#endif
