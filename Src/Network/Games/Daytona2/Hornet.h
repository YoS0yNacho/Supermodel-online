// Original Daytona USA 2: native Hornet shortcut at COURSE confirmation.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_DAYTONA2_HORNET_H
#define INCLUDED_DAYTONA2_HORNET_H
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace Daytona2Hornet {
// Exact-ROM course-confirm hook; brake selects Hornet without changing other controls.
constexpr uint32_t Address=0x347dc, MapAddress=0xb1090, CarAddress=0x34998;
constexpr uint32_t Original[]={0x3821fff0,0x7c0802a6,0x90010000,0x8a4f0040,0x3c400010,0x9a425fc5,0x3860000a,0x3c400010,0x98625008,0x80010000,0x0cc00200,0x7c0803a6,0x38210010,0x4e800020};
// mflr r0 | lbz r18,0x40(r15) | lis r2,0x10 | stb r18,0x5fc5(r2) |
// lwz r3,0x5488(r2) | cmpwi r3,0x80 | blt +12 | li r3,8 | stb r3,0x6f(r15) |
// li r3,10 | stb r3,0x5008(r2) | blr | nop | nop
constexpr uint32_t BrakeAddress=0x105488, BrakeHeld=0x80;
constexpr uint32_t Patched[]={0x7c0802a6,0x8a4f0040,0x3c400010,0x9a425fc5,0x80625488,0x2c030080,0x4180000c,0x38600008,0x986f006f,0x3860000a,0x98625008,0x4e800020,0x60000000,0x60000000};
static_assert(Patched[2]==(0x3c400000u|(BrakeAddress>>16)) && Patched[4]==(0x80620000u|(BrakeAddress&0xffffu)) && Patched[5]==(0x2c030000u|BrakeHeld),"brake load/compare words");
// Car confirmation remains byte-for-byte original, including Start handling.
constexpr uint32_t OriginalCar[]={0x8a4f0040,0x3c60000b,0x38631090,0x7c7218ae,0x3c400010,0x98625fc7,0x806f0044,0x38800001,0x3884001b};
enum class Result { Disabled, WrongGame, NotReady, UnexpectedInstruction, Applied, AlreadyApplied };
inline uint32_t Read(const uint8_t* p){uint32_t v;std::memcpy(&v,p,4);return v;}
inline Result Apply(const char* game,bool enabled,uint8_t* ram,size_t size){
 if(!enabled)return Result::Disabled;
 if(!game||std::strcmp(game,"daytona2"))return Result::WrongGame;
 if(!ram||size<MapAddress+4||size<Address+sizeof(Original)+4||size<CarAddress+sizeof(OriginalCar))return Result::NotReady;
 if(!Read(ram+Address)&&!Read(ram+Address-4))return Result::NotReady;
 // Refuse other revisions, incomplete relocation and the obsolete car shortcut.
 if(Read(ram+Address-4)!=0x4e800020||Read(ram+Address+sizeof(Original))!=0x3821fff0||Read(ram+MapAddress)!=0x00010203||Read(ram+0xb0abc)!=Address)return Result::UnexpectedInstruction;
 for(size_t i=0;i<sizeof(OriginalCar)/4;++i)if(Read(ram+CarAddress+i*4)!=OriginalCar[i])return Result::UnexpectedInstruction;
 bool original=true,patched=true;
 for(size_t i=0;i<sizeof(Original)/4;++i){const auto word=Read(ram+Address+i*4);original&=word==Original[i];patched&=word==Patched[i];}
 if(patched)return Result::AlreadyApplied;
 if(!original)return Result::UnexpectedInstruction;
 std::memcpy(ram+Address,Patched,sizeof(Patched));return Result::Applied;
}
}
#endif
