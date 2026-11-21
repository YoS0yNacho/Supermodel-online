// ROM-free runtime timing/capacity invariants. GPL-3.0-or-later.
#include "Network/Online/SessionPolicy.h"
#include "Network/Online/LobbyClient.h"
#include "Network/Games/Policy.h"
#include "Network/Games/NativeRaceMembership.h"
#include <string>
#include <utility>
#include <cassert>
#include <cmath>
#include <iostream>
int main() {
  constexpr unsigned capacities[] = {16,16,8,8,4,6,8,4,16,8,4,8,4};
  static_assert(sizeof(capacities)/sizeof(*capacities)==Online::GameCount);
  for (unsigned game=0;game<Online::GameCount;++game) {
    const unsigned expected=capacities[game];
    assert(Online::MaxPlayers(game)==expected);
    if(game==Sgt24hOnline::Game) {
      assert(std::string(Online::RomGame(game))=="sgt24h");
      assert(Online::Model2RelayGame(game));
      assert(!NativeRoomSettings::Supported(game) && NativeRoomSettings::Defaults(game)==0);
      assert(!NativeRoomSettings::CarLimit(game) && !NativeRoomSettings::SupportsRaceModifiers(game));
      for(unsigned options=0;options<(1u<<14);++options)
        assert(NativeRoomSettings::Valid(game,options)==(options==0));
    } else if(game==Indy500Online::Game) {
      assert(std::string(Online::RomGame(game))=="indy500" && !GameNetwork::IsSupportedGame(Online::RomGame(game)));
      namespace Room = NativeRoomSettings;
      assert(Room::Supported(game) && !Room::CarLimit(game) && !Room::SupportsRaceModifiers(game));
      assert(Room::DifficultyCount(game)==5 && Room::ModeCount(game)==2);
      const auto defaults=Room::Defaults(game);
      assert(Room::Difficulty(defaults)==2 && Room::Mode(defaults)==0 && Room::HasIndyHandicap(defaults));
      for(uint32_t options=0;options<1u<<14;++options) {
        const auto native=options & ~Room::Video60;
        bool allowed=native==0;
        for(uint32_t difficulty=0;difficulty<5;++difficulty) for(uint32_t mode=0;mode<2;++mode)
          for(uint32_t handicap=0;handicap<2;++handicap)
            allowed|=native==(2u|(difficulty<<2)|(mode<<5)|(handicap<<11));
        assert(Room::Valid(game,options)==allowed);
      }
    } else if (game==SrallycOnline::Game || game==DaytonaOnline::Game) {
      assert(std::string(Online::RomGame(game))==(game==DaytonaOnline::Game?"daytona":"srallyc") && !GameNetwork::IsSupportedGame(Online::RomGame(game)));
      namespace Room = NativeRoomSettings;
      assert(Online::Model2RelayGame(game) && Room::Supported(game) && !Room::CarLimit(game));
      assert(Room::DifficultyCount(game)==4 && Room::ModeCount(game)==3);
      assert(Room::Defaults(game)==6 && !Room::HasVideo60(Room::Defaults(game)));
      for (uint32_t options=0;options<1u<<14;++options) {
        bool allowed=false;
        for(uint32_t difficulty=0;difficulty<4;++difficulty) for(uint32_t mode=0;mode<3;++mode)
          allowed|=(options & ~Room::Video60)==(2u|(difficulty<<2)|(mode<<5));
        assert(Room::Valid(game,options)==allowed);
      }
    } else {
      assert(!Online::Model2RelayGame(game));
      assert(GameNetwork::MaxCabinets(Online::RomGame(game),game==6||game==8)==expected);
    }
  }
  for(bool vsync:{false,true}) for(float configuredHz : {50.0f,57.524f,60.0f,120.0f}) {
    Util::Config::Node config("Global");
    config.Set("NetRelay",std::string());
    config.Set("RefreshRate",configuredHz);config.Set("Throttle",false);config.Set("VSync",vsync);
    Online::EnforceRelayTiming(config);
    assert(config["RefreshRate"].ValueAs<float>()==configuredHz);
    assert(!config["Throttle"].ValueAs<bool>()&&config["VSync"].ValueAs<bool>()==vsync);
    config.Set("NetRelay",std::string("127.0.0.1:1970"));
    Online::EnforceRelayTiming(config);
    // Online: 57.524 Hz by default, throttle mandatory, VSync left as configured.
    assert(std::abs(config["RefreshRate"].ValueAs<float>()-57.524f)<0.0001f);
    assert(config["Throttle"].ValueAs<bool>()&&config["VSync"].ValueAs<bool>()==vsync);
    assert(!Online::RelayVideo60(config));
    config.Set("NetNativeRoomOptions",NativeRoomSettings::Defaults(Indy500Online::Game));
    Online::EnforceRelayTiming(config);
    assert(!Online::RelayVideo60(config) && std::abs(config["RefreshRate"].ValueAs<float>()-57.524f)<0.0001f);
    // The master's Video60 room option (bit 1024) switches the whole room to 60 Hz.
    config.Set("NetNativeRoomOptions",unsigned(NativeRoomSettings::Video60|2u|4u));config.Set("Throttle",false);
    Online::EnforceRelayTiming(config);
    assert(Online::RelayVideo60(config)&&config["RefreshRate"].ValueAs<float>()==60.0f);
    assert(config["Throttle"].ValueAs<bool>()&&config["VSync"].ValueAs<bool>()==vsync);
  }
  // Each native Model 2 title preserves its hardware default and the same
  // runtime-only selector, even if an offline INI sets a different rate.
  for (const auto& [game, nativeHz] : {std::pair{Indy500Online::Game, Indy500Online::RefreshRate},
                                      std::pair{SrallycOnline::Game, SrallycOnline::RefreshRate},
                                      std::pair{DaytonaOnline::Game, DaytonaOnline::RefreshRate}}) {
    for (bool vsync : {false, true}) for (bool video60 : {false, true}) {
      auto options=NativeRoomSettings::Defaults(game) | (video60 ? NativeRoomSettings::Video60 : 0u);
      assert(NativeRoomSettings::Valid(game,options));
      Util::Config::Node config("Native Model 2");
      config.Set("NetRelay",std::string("127.0.0.1:31970"));
      config.Set("NetNativeRoomOptions",options); config.Set("RefreshRate",120.0f);
      config.Set("Throttle",false); config.Set("VSync",vsync);
      Online::EnforceRelayTiming(config,nativeHz);
      assert(config["RefreshRate"].ValueAs<float>()==(video60 ? 60.0f : nativeHz));
      assert(config["Throttle"].ValueAs<bool>() && config["VSync"].ValueAs<bool>()==vsync);
    }
  }
  // Model 3 Video60 is valid with or without shared native settings; nothing above bit 10 is.
  for(unsigned game=0;game<Indy500Online::Game;++game){
    assert(NativeRoomSettings::Valid(game,NativeRoomSettings::Video60));
    assert(NativeRoomSettings::Valid(game,NativeRoomSettings::Defaults(game)|NativeRoomSettings::Video60));
    assert(!NativeRoomSettings::Valid(game,2048u));
  }
  { // Stock and 16-player Scud Race Plus share room options, not car-number limits.
    using namespace NativeRoomSettings;
    constexpr unsigned stock=3, wide=8;
    assert(BaseGame(wide)==stock && CarLimit(stock)==8 && CarLimit(wide)==16);
    assert(Defaults(wide)==Defaults(stock));
    assert(DifficultyCount(wide)==DifficultyCount(stock) && ModeCount(wide)==ModeCount(stock));
    for(uint32_t options=0;options<=AllBits;++options)
      assert(Valid(stock,options)==Valid(wide,options));
    for(unsigned game:{stock,wide}) {
      assert(SupportsSecretCars(game) && SupportsRaceModifiers(game));
      const uint32_t options=WithMode(WithDifficulty(Defaults(game),3),2)|SecretCars|RealPlayers|NoCollisions|Video60;
      assert(Valid(game,options));
      assert(HasSecretCars(game,options) && HasRealPlayers(game,options) && HasNoCollisions(game,options) && HasVideo60(options));
    }
    assert(HasHornet(0,Hornet) && !HasSecretCars(0,Hornet) && !SupportsSecretCars(2));
    assert(!SupportsRaceModifiers(6) && !Valid(7,Defaults(7)|Enabled));
  }
  assert(Online::RomGame(8)==std::string("scudplus") && GameNetwork::GUID("scudplus",true)==ScudResearch::Guid && GameNetwork::GUID("scudplus")==0x5bf177da34877ULL);
  assert(GameNetwork::MaxCabinets("scudplus")==8 && GameNetwork::MaxCabinets("scudplus",true)==16 && GameNetwork::MaxCabinets("scud",true)==8 && GameNetwork::GUID("scud",true)==0x5bf177da34876ULL);
  { // the research patch only claims the exact Export Rev A fixed CROM; anything else is refused before any write
    std::array<uint8_t,Scud::SixteenPlayerPpcResearch::FixedCromSize> rom{};
    assert(Scud::SixteenPlayerPpcResearch::ApplyBigEndianCrom(rom.data(),rom.size(),"scudplus")==Scud::SixteenPlayerPpcResearch::Result::Disabled);
    assert(Scud::SixteenPlayerPpcResearch::ApplyBigEndianCrom(rom.data(),rom.size(),"scud",true)==Scud::SixteenPlayerPpcResearch::Result::UnsupportedGame);
    assert(Scud::SixteenPlayerPpcResearch::ApplyBigEndianCrom(rom.data(),rom.size()-4,"scudplus",true)==Scud::SixteenPlayerPpcResearch::Result::WrongSize);
    assert(Scud::SixteenPlayerPpcResearch::ApplyBigEndianCrom(rom.data(),rom.size(),"scudplus",true)==Scud::SixteenPlayerPpcResearch::Result::UnexpectedOpcode);
    for(const auto& site:Scud::SixteenPlayerPpcResearch::Sites) Scud::SixteenPlayerPpcResearch::WriteBE(rom.data()+(site.pc-Scud::SixteenPlayerPpcResearch::FixedCromBase),site.expected);
    assert(Scud::SixteenPlayerPpcResearch::ApplyBigEndianCrom(rom.data(),rom.size(),"scudplus",true)==Scud::SixteenPlayerPpcResearch::Result::WrongCromCRC);
    for(const auto& site:Scud::SixteenPlayerPpcResearch::Sites) Scud::SixteenPlayerPpcResearch::WriteBE(rom.data()+(site.pc-Scud::SixteenPlayerPpcResearch::FixedCromBase),site.replacement);
    assert(Scud::SixteenPlayerPpcResearch::ApplyBigEndianCrom(rom.data(),rom.size(),"scudplus",true)==Scud::SixteenPlayerPpcResearch::Result::Applied); // already patched image (second LoadROM pass)
  }
  {
    std::array<uint8_t,16> owners{}; for(unsigned i=0;i<16;++i) owners[i]=uint8_t(i+1);
    assert(GameNetwork::ScudRaceParticipants(16,16,16,owners)==0xffffu && GameNetwork::ScudRaceParticipants(8,1,8,owners)==0xffu);
    std::array<uint8_t,8> stock{}; for(unsigned i=0;i<8;++i) stock[i]=uint8_t(i+1);
    assert(GameNetwork::ScudRaceParticipants(9,1,8,stock)==0 && GameNetwork::ScudRaceParticipants(8,1,8,stock)==0xffu);
  }
  assert(std::abs(Online::FrameDurationUs(true)-17384.048397)<0.01);
  assert(Online::FrameMilliHz(true,true)==60000u&&Online::FrameMilliHz(false,true)==60000u&&Online::FrameMilliHz(true,false)==57524u);
  for(bool relay:{false,true}) for(unsigned hz:{5u,15u,20u,24u,57u,60u}) {
    const unsigned frameHz=Online::FrameMilliHz(relay);
    unsigned phase=0,sent=0;
    // frameHz frames span exactly 1000 seconds. One send max per frame.
    for(unsigned frame=0;frame<frameHz;++frame) {
      phase=Online::AccumulateTick(phase,hz,frameHz);
      if(phase>=frameHz) {++sent;phase=Online::ConsumeTick(phase,frameHz);}
    }
    const unsigned expected=hz*1000u<frameHz?hz*1000u:frameHz;
    assert(sent==expected);
    assert(Online::ConsumeTick(10u*frameHz,frameHz)==frameHz-1u);
  }
  std::cout<<"PASS Model 2/3 capacity/options, runtime-only 57.524/60 Hz room video rate, throttle policy with VSync preserved, real-time 5..60 Hz send pacing\n";
}
