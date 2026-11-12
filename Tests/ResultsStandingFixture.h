// Shared native-memory fixtures, not a substitute for a native-ROM race.
// GPL-3.0-or-later
#pragma once
#include "Network/Online/LobbyClient.h"
#include <cassert>
#include <vector>
struct ResultsStandingFixture {
  unsigned game;
  std::vector<uint8_t> ram = std::vector<uint8_t>(0x800000);
  explicit ResultsStandingFixture(unsigned g):game(g) { Race(); Rank(4); }
  uint8_t Read8(uint32_t a) const { assert(a<ram.size()); return ram[a]; }
  uint32_t Read32(uint32_t a) const {
    return (uint32_t(Read8(a))<<24)|(uint32_t(Read8(a+1))<<16)|(uint32_t(Read8(a+2))<<8)|Read8(a+3);
  }
  void Word(uint32_t a,uint32_t v) { for(int i=3;i>=0;--i) {ram[a+i]=uint8_t(v);v>>=8;} }
  std::vector<unsigned> ResultPhases() const {
    if(game==0)return {18,19};
    if(game==1)return {18,19,28,29};
    if(game<4)return {22,23,30,31};
    if(game==4||game==6)return {11,12,13};
    return {0,1,2,3,4,5}; // Stage4 substates, not racing Stage3
  }
  void Phase(unsigned p) {
    if(game<2)ram[0x105004]=uint8_t(p);
    else if(game<4)ram[0x104004]=uint8_t(p);
    else if(game==4||game==6)Word(0x4c21e8,p);
    else Word(0x18d298,p);
  }
  void Race() {
    if(game<2) {Phase(16);Word(game==0?0x105898:0x737a7c,0x200000);}
    else if(game<4) {Phase(19);Word(0x1045a0,0x200000);Word(0x200008,0x300);}
    else if(game==4||game==6) {Word(0x4c21d8,1);Word(0x4c21dc,1);Phase(10);}
    else {Word(0x17f61c,3);Word(0x17e7e0,5);Word(0x1604f0,0);Phase(3);}
  }
  void Results(unsigned phase) {if(game==5)Word(0x17f61c,4);Phase(phase);}
  void Inactive() {if(game==5)Word(0x17f61c,0);else Phase(0);}
  void Rank(uint32_t normalized) {
    if(game<2)ram[0x2000c9]=normalized?uint8_t(normalized-1):255;
    else if(game<4)ram[0x2000d4]=normalized?uint8_t(normalized-1):255;
    else Word(game==5?0x17fe60:0x225a14,normalized);
  }
  Online::NativeStanding Sample() {return Online::ReadNativeStanding(*this,Online::RomGame(game));}
};
