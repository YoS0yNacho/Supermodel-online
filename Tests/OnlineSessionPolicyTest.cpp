// ROM-free runtime timing/capacity invariants. GPL-3.0-or-later.
#include "Network/Online/SessionPolicy.h"
#include "Network/Online/LobbyClient.h"
#include "Network/Games/Policy.h"
#include <cassert>
#include <cmath>
#include <iostream>
int main() {
  for (unsigned game=0;game<Online::GameCount;++game) {
    const unsigned expected=game<2?16:game==4?4:game==5?6:8;
    assert(Online::MaxPlayers(game)==expected);
    assert(GameNetwork::MaxCabinets(Online::Games[game])==expected);
  }
  for(float configuredHz : {50.0f,57.524f,60.0f,120.0f}) {
    Util::Config::Node config("Global");
    config.Set("NetRelay",std::string());
    config.Set("RefreshRate",configuredHz);config.Set("Throttle",false);config.Set("VSync",true);
    Online::EnforceRelayTiming(config);
    assert(config["RefreshRate"].ValueAs<float>()==configuredHz);
    assert(!config["Throttle"].ValueAs<bool>()&&config["VSync"].ValueAs<bool>());
    config.Set("NetRelay",std::string("127.0.0.1:1970"));
    Online::EnforceRelayTiming(config);
    assert(std::abs(config["RefreshRate"].ValueAs<float>()-57.524f)<0.0001f);
    assert(config["Throttle"].ValueAs<bool>()&&!config["VSync"].ValueAs<bool>());
  }
  assert(std::abs(Online::FrameDurationUs(true)-17384.048397)<0.01);
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
  std::cout<<"PASS six-game capacity, runtime-only mandatory 57.524 Hz, VSync/throttle policy, real-time 5..60 Hz send pacing\n";
}
