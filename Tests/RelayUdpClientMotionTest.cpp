// Regression for a delta overtaking two ordered TCP keyframes on relay star fan-out.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Network/RelayUpnp.h"
#include "Network/RelayUdpHost.h"
#include "Network/Online/AdaptiveTickRate.h"
#include "SDLIncludes.h"
#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>
// Restrict transport injection to this test, after loading the real dependencies.
#define private public
#include "Network/RelayUdpClient.h"
#undef private
#include <cstdio>

int main()
{
#define REQUIRE(test) do { if (!(test)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#test); return 1; } } while (false)
  REQUIRE(SDLNet_Init() == 0);
  const uint8_t token[16]{};
  {
    CRelayUdpClient client("127.0.0.1",1970,token,0,2,4);
    REQUIRE(client.Available());
    client.m_stop=true;
    client.m_thread.join();
    // One origin only. The initial base is available, but the later delta
    // was built on base 300. An ordered frame at 200 must not discard it.
    const uint8_t first[]={1,2,3,4}, older[]={5,6,7,8}, base[]={9,10,11,12};
    const std::vector<uint8_t> car{28,29,30,31};
    client.SetPeerBase(1,100,first,4);
    auto park=[&](uint64_t baseUs,uint64_t sourceUs) {
      auto& h=client.m_held[1];
      h={};h.raw=car;h.timed=true;h.baseUs=baseUs;h.sourceUs=sourceUs;h.receivedUs=CRelayUdpClient::Now();
      for(size_t i=0;i<h.raw.size();++i) h.raw[i]^=base[i];
    };
    park(300,400);
    const auto received=client.m_held[1].receivedUs;
    client.SetPeerBase(1,200,older,4);
    CRelayUdpClient::Sample s;
    REQUIRE(!client.Latest(1,s));
    REQUIRE(client.m_held[1].baseUs==300 && !client.m_held[1].raw.empty());
    client.SetPeerBase(1,300,base,4);
    REQUIRE(client.Latest(1,s));
    REQUIRE(s.raw==car && s.sourceUs==400 && s.receivedUs==received && s.timed);
    REQUIRE(client.m_held[1].raw.empty());
    REQUIRE(client.GetStats().heldResolved==1);
    park(500,550);
    client.SetPeerBase(1,600,older,4); // missing base is now superseded
    REQUIRE(client.m_held[1].raw.empty());
    REQUIRE(!client.Latest(1,s,400));
    REQUIRE(client.GetStats().deltaStale==1);
    // The timestamp fix must not extend the genuine loss/fallback window.
    const auto now=CRelayUdpClient::Now();
    REQUIRE(now>client.LeaveUs());
    client.m_lastAck=now;
    client.m_alive[1]={now,true};
    client.m_heard[1]=1;
    client.m_heardMeUs[1]=now;
    REQUIRE(client.PrimaryReady() && client.PrimaryReady(true));
    client.m_heardMeUs[1]=now-client.LeaveUs()-1;
    REQUIRE(!client.PrimaryReady(true));
    client.m_heardMeUs[1]=now;
    client.m_alive[1].receivedUs=now-client.LeaveUs()-1;
    REQUIRE(!client.PrimaryReady() && !client.PrimaryReady(true));
  }
  SDLNet_Quit();
  std::puts("PASS: an older TCP frame retains the future-base delta; true UDP outages still fall back");
}
