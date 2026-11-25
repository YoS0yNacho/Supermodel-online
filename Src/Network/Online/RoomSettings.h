// Native room settings wire schema. Session-only; never a transport node ID.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_ONLINE_ROOM_SETTINGS_H
#define INCLUDED_ONLINE_ROOM_SETTINGS_H
#include "Indy500.h"
#include "Srallyc.h"
#include "Daytona.h"
#include "Sgt24h.h"
#include <cstdint>
namespace NativeRoomSettings {
// Bit0 is game-specific: original Daytona Hornet or Scud Plus Secret Cars.
// Bit8 (256) is NoCollisions: all cars ghost through each other.
// Bit9 (512) is RealPlayers: no CPU cars; solo play is native time attack.
// Bit10 (1024) is Video60: every cabinet emulates 60 Hz video instead of the
// 57.524 Hz original rate; chosen by the master for the whole room.
// Bit12 (4096) is NoPitstop: keep the pit lane open as a shortcut; the
// automatic pit entry/service transition is skipped for Daytona 1.
constexpr uint32_t SecretCars=1, Hornet=1, Enabled=2, NoCollisions=256, RealPlayers=512, Video60=1024, NoPitstop=4096;
constexpr uint32_t AllBits=8191; // Model 3 only
constexpr uint32_t IndyHandicap=1u<<11;
constexpr uint32_t SrallycSettingsBits=Enabled|28u|224u;
constexpr uint32_t IndySettingsBits=Enabled|28u|224u|IndyHandicap;
constexpr uint32_t DaytonaSettingsBits=Enabled|28u|224u|RealPlayers|NoCollisions|NoPitstop;
inline constexpr bool Model2Game(unsigned game){return game==Sgt24hOnline::Game || game==Indy500Online::Game || game==SrallycOnline::Game || game==DaytonaOnline::Game;}
inline bool HasIndyHandicap(uint32_t options){return (options&IndyHandicap)!=0;}
inline bool HasVideo60(uint32_t options){return (options&Video60)!=0;}
inline bool HasHornet(unsigned game,uint32_t options){return game==0 && (options&Hornet);}
// Lobby indices 6 (Sega Rally 2 - 8 Player) and 8 (Scud Race Plus - 16 Player)
// share every native setting of their stock game (4 and 3).
inline unsigned BaseGame(unsigned game){return game==6?4:game==8?3:game;}
inline bool SupportsSecretCars(unsigned game){return BaseGame(game)==3;}
inline bool SupportsRaceModifiers(unsigned game){return BaseGame(game)<4 || game==DaytonaOnline::Game;}
inline bool HasSecretCars(unsigned game,uint32_t options){return SupportsSecretCars(game) && (options&SecretCars);}
inline bool HasRealPlayers(unsigned game,uint32_t options){return SupportsRaceModifiers(game) && (options&RealPlayers);}
inline bool HasNoCollisions(unsigned game,uint32_t options){return SupportsRaceModifiers(game) && (options&NoCollisions);}
inline bool HasNoPitstop(unsigned game,uint32_t options){return SupportsRaceModifiers(game) && (options&NoPitstop);}
// Game 7 (dirtdvls) has no native settings: like Le Mans 24 only Video60 applies.
inline bool Supported(unsigned game){return game<5 || game==6 || game==8 || game==Indy500Online::Game || game==SrallycOnline::Game || game==DaytonaOnline::Game;}
inline unsigned CarLimit(unsigned game){return game<2?16:game==8?16:game<4?8:game==DaytonaOnline::Game?8:0;}
inline unsigned DifficultyCount(unsigned game){return BaseGame(game)<4 || game==SrallycOnline::Game || game==DaytonaOnline::Game?4:BaseGame(game)==4 || game==Indy500Online::Game?5:0;}
inline unsigned ModeCount(unsigned game){return game<2?7:(BaseGame(game)<4 || game==SrallycOnline::Game || game==DaytonaOnline::Game)?3:BaseGame(game)==4 || game==Indy500Online::Game?2:0;}
inline unsigned Difficulty(uint32_t options){return (options>>2)&7;}
inline unsigned Mode(uint32_t options){return (options>>5)&7;}
inline bool Valid(unsigned game,uint32_t options){
 if(game==Sgt24hOnline::Game)return options==0;
 // The rate bit is independent of operator settings. In particular, Indy
 // may keep its original EEPROM rules while using the 60 Hz room clock.
 if(Model2Game(game))options&=~Video60;
 if(game==Indy500Online::Game)
  return options==0 || ((options&Enabled) && !(options&~IndySettingsBits) &&
                        Difficulty(options)<DifficultyCount(game) && Mode(options)<ModeCount(game));
 if(game==SrallycOnline::Game)
  return (options&Enabled) && !(options&~SrallycSettingsBits) &&
         Difficulty(options)<DifficultyCount(game) && Mode(options)<ModeCount(game);
 if(game==DaytonaOnline::Game)
  return (options&Enabled) && !(options&~DaytonaSettingsBits) &&
         Difficulty(options)<DifficultyCount(game) && Mode(options)<ModeCount(game);
 if(game>=Indy500Online::Game || (options&~AllBits) || ((options&SecretCars)&&game!=0&&!SupportsSecretCars(game)))return false;
 if((options&(RealPlayers|NoCollisions)) && !SupportsRaceModifiers(game))return false;
 if(!(options&Enabled))return !(options&~(SecretCars|RealPlayers|NoCollisions|Video60));
 return Supported(game) && Difficulty(options)<DifficultyCount(game) && Mode(options)<ModeCount(game);
}
inline uint32_t Defaults(unsigned game){
 if(game==Indy500Online::Game)return Enabled|(2u<<2)|IndyHandicap; // Normal / Normal / On
 if(game==DaytonaOnline::Game)return Enabled|(1u<<2); // Normal / Normal
 if(game==SrallycOnline::Game)return Enabled|(1u<<2); // Normal / Normal
 return Supported(game)?Enabled|(BaseGame(game)<4?4u:0u):0;
}
inline uint32_t WithDifficulty(uint32_t options,unsigned difficulty){return (options&~28u)|Enabled|(difficulty<<2);}
inline uint32_t WithMode(uint32_t options,unsigned mode){return (options&~224u)|Enabled|(mode<<5);}
inline const char* DifficultyName(unsigned game,unsigned value){
 static const char* usual[]={"Easy","Normal","Hard","Hardest"};
 // Rally stores its five service choices in this non-monotonic native order.
 static const char* rally[]={"Normal","Hard","Very hard","Very easy","Easy"};
 static const char* indy[]={"Very Easy","Easy","Normal","Hard","Hardest"};
 return value<DifficultyCount(game)?(game==Indy500Online::Game?indy[value]:BaseGame(game)==4?rally[value]:usual[value]):"Invalid";
}
inline const char* ModeName(unsigned game,unsigned value){
 static const char* daytona[]={"Normal (Sprint)","Grand Prix","100 Miles","200 Miles","300 Miles","400 Miles","500 Miles"};
 static const char* scud[]={"Normal (Sprint)","Grand Prix","Endurance"};
 static const char* rally[]={"Normal","Special"};
 static const char* indy[]={"Normal","Long"};
 static const char* srallyc[]={"Normal","Short","Long"};
 return value<ModeCount(game)?(game==Indy500Online::Game?indy[value]:game==SrallycOnline::Game?srallyc[value]:game==DaytonaOnline::Game?scud[value]:game<2?daytona[value]:BaseGame(game)<4?scud[value]:rally[value]):"Invalid";
}
}
#endif
