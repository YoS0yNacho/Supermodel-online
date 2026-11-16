// ROM-free runtime timing/capacity invariants. GPL-3.0-or-later.
#include "Network/Online/SessionPolicy.h"
#include "Network/Online/LobbyClient.h"
#include "Network/Games/Policy.h"
#include "Network/Games/NativeRaceMembership.h"
#include <string>
#include <cassert>
#include <cmath>
#include <iostream>
int main() {
  for (unsigned game=0;game<Online::GameCount;++game) {
    const unsigned expected=game<2?16:game==4?4:game==5?6:game==7?4:game==8?16:8; // dirtdvls: MACHINE ID 1..4; scudplus_16: research patch
    assert(Online::MaxPlayers(game)==expected);
    assert(GameNetwork::MaxCabinets(Online::RomGame(game),game==6||game==8)==expected);
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
    // The master's Video60 room option (bit 1024) switches the whole room to 60 Hz.
    config.Set("NetNativeRoomOptions",unsigned(NativeRoomSettings::Video60|2u|4u));config.Set("Throttle",false);
    Online::EnforceRelayTiming(config);
    assert(Online::RelayVideo60(config)&&config["RefreshRate"].ValueAs<float>()==60.0f);
    assert(config["Throttle"].ValueAs<bool>()&&config["VSync"].ValueAs<bool>()==vsync);
  }
  // Video60 is valid for every game, with or without shared native settings; nothing above bit 10 is.
  for(unsigned game:{0u,1u,2u,3u,4u,5u,6u,8u}){
    assert(NativeRoomSettings::Valid(game,NativeRoomSettings::Video60));
    assert(NativeRoomSettings::Valid(game,NativeRoomSettings::Defaults(game)|NativeRoomSettings::Video60));
    assert(!NativeRoomSettings::Valid(game,2048u));
  }
  // Scud Race Plus 16-player rooms: Scud settings, 16 cabinets/car numbers, secret cars allowed.
  assert(NativeRoomSettings::BaseGame(8)==3 && NativeRoomSettings::CarLimit(8)==16 && NativeRoomSettings::Valid(8,NativeRoomSettings::Defaults(8)|NativeRoomSettings::SecretCars));
  assert(NativeRoomSettings::HasRealPlayers(8,NativeRoomSettings::RealPlayers) && !NativeRoomSettings::Valid(7,NativeRoomSettings::Defaults(7)|2u));
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
  std::cout<<"PASS nine-game capacity (incl. Scud Race Plus 16-player), runtime-only 57.524/60 Hz room video rate, throttle policy with VSync preserved, real-time 5..60 Hz send pacing\n";
}
