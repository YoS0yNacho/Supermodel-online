// Native room settings wire schema. Session-only; never a transport node ID.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_ONLINE_ROOM_SETTINGS_H
#define INCLUDED_ONLINE_ROOM_SETTINGS_H
#include <cstdint>
namespace NativeRoomSettings {
// Bit0 is game-specific: original Daytona Hornet or Scud Plus Secret Cars.
// Bit8 (256) is NoCollisions: all cars ghost through each other.
// Bit9 (512) is RealPlayers: no CPU cars; solo play is native time attack.
// Bit10 (1024) is Video60: every cabinet emulates 60 Hz video instead of the
// 57.524 Hz original rate. All games; chosen by the master for the whole room.
constexpr uint32_t SecretCars=1, Hornet=1, Enabled=2, NoCollisions=256, RealPlayers=512, Video60=1024;
constexpr uint32_t AllBits=2047;
inline bool HasVideo60(uint32_t options){return (options&Video60)!=0;}
inline bool HasHornet(unsigned game,uint32_t options){return game==0 && (options&Hornet);}
inline bool HasRealPlayers(unsigned game,uint32_t options){return game<4 && (options&RealPlayers);}
inline bool HasNoCollisions(unsigned game,uint32_t options){return game<4 && (options&NoCollisions);}
inline unsigned BaseGame(unsigned game){return game==6?4:game;}
inline bool Supported(unsigned game){return game<5 || game==6;}
inline unsigned CarLimit(unsigned game){return game<2?16:game<4?8:0;}
inline unsigned DifficultyCount(unsigned game){return game<4?4:(game==4||game==6)?5:0;}
inline unsigned ModeCount(unsigned game){return game<2?7:game<4?3:(game==4||game==6)?2:0;}
inline unsigned Difficulty(uint32_t options){return (options>>2)&7;}
inline unsigned Mode(uint32_t options){return (options>>5)&7;}
inline bool Valid(unsigned game,uint32_t options){
 if(game>=7 || (options&~AllBits) || ((options&SecretCars)&&game!=0&&game!=3))return false;
 if((options&(RealPlayers|NoCollisions)) && game>=4)return false;
 if(!(options&Enabled))return !(options&~(SecretCars|RealPlayers|NoCollisions|Video60));
 return Supported(game) && Difficulty(options)<DifficultyCount(game) && Mode(options)<ModeCount(game);
}
inline uint32_t Defaults(unsigned game){return Supported(game)?Enabled|(game<4?4u:0u):0;}
inline uint32_t WithDifficulty(uint32_t options,unsigned difficulty){return (options&~28u)|Enabled|(difficulty<<2);}
inline uint32_t WithMode(uint32_t options,unsigned mode){return (options&~224u)|Enabled|(mode<<5);}
inline const char* DifficultyName(unsigned game,unsigned value){
 static const char* usual[]={"Easy","Normal","Hard","Hardest"};
 // Rally stores its five service choices in this non-monotonic native order.
 static const char* rally[]={"Normal","Hard","Very hard","Very easy","Easy"};
 return value<DifficultyCount(game)?(BaseGame(game)==4?rally[value]:usual[value]):"Invalid";
}
inline const char* ModeName(unsigned game,unsigned value){
 static const char* daytona[]={"Normal (Sprint)","Grand Prix","100 Miles","200 Miles","300 Miles","400 Miles","500 Miles"};
 static const char* scud[]={"Normal (Sprint)","Grand Prix","Endurance"};
 static const char* rally[]={"Normal","Special"};
 return value<ModeCount(game)?(game<2?daytona[value]:game<4?scud[value]:rally[value]):"Invalid";
}
}
#endif
