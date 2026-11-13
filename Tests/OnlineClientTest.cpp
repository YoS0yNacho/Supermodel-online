#include "ResultsStandingFixture.h"
// Actual C++ launcher client + actual CNetBoardLink, no ROMs. GPL-3.0-or-later.
#include "Network/Online/LobbyClient.h"
#include "Network/RelayProtocol.h"
#include "Model3/NetBoardLink.h"
#include "OSD/Logger.h"
#include <cassert>
#include <chrono>
#include <thread>
#include <iostream>
#include <cstdarg>
Result ErrorLog(const char* fmt,...) { va_list a;va_start(a,fmt);vfprintf(stderr,fmt,a);va_end(a);fputc('\n',stderr);return Result::FAIL; }
void InfoLog(const char*,...) {}
void DebugLog(const char*,...) {}
template<class F> void Wait(F f) {
  const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(12);
  while(!f()) {assert(std::chrono::steady_clock::now()<end);std::this_thread::sleep_for(std::chrono::milliseconds(10));}
}
int main(int argc,char**argv) {
  assert(argc==2);uint16_t port=uint16_t(std::stoi(argv[1]));std::string endpoint="127.0.0.1:"+std::to_string(port);
  {
    std::string defHost; uint16_t defPort=0;
    // Defaults moved to ServerDefaults.h: check consistency, not a hard-coded host.
    assert(std::string(Online::DefaultRelayEndpoint)==std::string(Online::DefaultRelayHost)+":"+std::to_string(Online::DefaultRelayPort));
    assert(std::string(Online::DefaultRelayEndpoints[0])==Online::DefaultRelayEndpoint);
    assert(Online::DefaultRelayPort==1970);
    assert(std::string(Online::DefaultPlayerName)=="Player");
    assert(RelayProtocol::ParseEndpoint(Online::DefaultRelayEndpoint,defHost,defPort)&&defHost==Online::DefaultRelayHost&&defPort==Online::DefaultRelayPort);
  }
  uint8_t ticket[16];assert(!RelayProtocol::ParseTicket("",ticket));assert(!RelayProtocol::ParseTicket(std::string(32,'0'),ticket));assert(!RelayProtocol::ParseTicket(std::string(32,'x'),ticket));assert(RelayProtocol::ParseTicket(std::string(32,'a'),ticket));
  Online::LobbyClient invalid;invalid.Connect("invalid:::address","test",Online::GameMaskAll,true);assert(!invalid.Get().error.empty());
  Online::LobbyClient cancelling;cancelling.Connect("192.0.2.1:1970","test",Online::GameMaskAll,true);
  const auto before=std::chrono::steady_clock::now();cancelling.Disconnect();assert(std::chrono::steady_clock::now()-before<std::chrono::milliseconds(100));
  Online::RoomChatClient badChat;badChat.Connect("invalid:::ep","abcd","P");assert(badChat.Get().closed&&!badChat.Get().error.empty());
  Online::RoomChatClient cancelChat;cancelChat.Connect("192.0.2.1:1970",std::string(32,'a'),"P");
  const auto beforeChat=std::chrono::steady_clock::now();cancelChat.Disconnect();assert(std::chrono::steady_clock::now()-beforeChat<std::chrono::milliseconds(100));
  const uint64_t guids[]={0x5bf177da34878,0x5bf177da34875,0x5bf177da34876,0x5bf177da34877,0x5bf177da34879,0x5bf177da3487a,0x5bf177da3487b};
  const size_t sizes[]={848,848,1024,1024,2048,3264,2048};
  static_assert(sizeof(guids)/sizeof(*guids)==Online::GameCount);
  static_assert(sizeof(sizes)/sizeof(*sizes)==Online::GameCount);
  for(unsigned game=0;game<Online::GameCount;++game) {
    Online::LobbyClient master,slave;
    master.Connect(endpoint,"C++ Master",Online::GameMaskAll,true);slave.Connect(endpoint,"C++ Slave",Online::GameMaskAll,true);
    Wait([&]{return master.Get().connected&&slave.Get().connected;});
    if(game==0) {
      master.SendChat("Anyone up for Daytona?");
      Wait([&]{return !slave.Get().loungeChat.empty()&&slave.Get().loungeChat.back().text=="Anyone up for Daytona?";});
    }
    master.Create(game,"C++ client test");Wait([&]{return master.Get().room!=0;});uint32_t room=master.Get().room;
    master.SendChat("Lobby ready!");
    Wait([&]{return !master.Get().roomChat.empty();});
    slave.Join(room);Wait([&]{return slave.Get().room==room&&!slave.Get().roomChat.empty();});
    assert(slave.Get().roomChat.back().car==1&&slave.Get().roomChat.back().text=="Lobby ready!");
    slave.SendChat("Joined as car 2!");
    Wait([&]{return master.Get().roomChat.size()>=2&&master.Get().roomChat.back().car==2;});
    if(game==0) {
      // Lobby ping: each client measures its own RTT from the OLQ2/OLP2 poll and the
      // relay lists every member's reported RTT (ORP1) in the room snapshot.
      auto roomPings=[&](const Online::Snapshot& v){std::vector<uint32_t> p;for(const auto& r:v.rooms)if(r.id==room)for(const auto& m:r.members)p.push_back(m.pingMs);return p;};
      auto both=[&]{auto p=roomPings(master.Get()),q=roomPings(slave.Get());return p.size()==2&&q.size()==2&&p[0]>0&&p[1]>0&&q[0]>0&&q[1]>0;};
      Wait([&]{return master.Get().pingMs>0&&slave.Get().pingMs>0&&both();});
      assert(master.Get().pingMs<=9999&&slave.Get().pingMs<=9999); // both views carry every member's reported RTT
    }
    slave.Start(room);Wait([&]{return !slave.Get().error.empty();});assert(!slave.Get().launch);
    master.Start(room);Wait([&]{return master.Get().launch&&slave.Get().launch;});
    auto a=master.Get(),b=slave.Get();assert(a.game==game&&b.game==game&&a.index==0&&b.index==1&&a.count==2&&b.count==2&&a.ticket!=b.ticket);
    master.Disconnect();slave.Disconnect(); // exactly the GUI -> emulator handoff
    Online::RoomChatClient chat0,chat1;
    chat0.Connect(endpoint,a.ticket,"C++ Master");chat1.Connect(endpoint,b.ticket,"C++ Slave");
    Wait([&]{return chat0.Get().connected&&chat1.Get().connected&&chat0.Get().messages.size()>=2&&chat1.Get().messages.size()>=2;});
    assert(chat0.Get().car==1&&chat1.Get().car==2);
    chat0.SetStanding({true, 4});chat1.SetStanding({true, 1});
    Wait([&]{const auto rows=chat0.Get().standings;return rows.size()==2&&rows[0].car==2&&rows[0].rank==1&&rows[1].rank==4;});
    assert(chat0.Get().standings[0].name=="C++ Slave");
    chat0.SetStanding({true, 1});chat1.SetStanding({true, 3});
    Wait([&]{const auto rows=chat1.Get().standings;return rows.size()==2&&rows[0].car==1&&rows[0].rank==1&&rows[1].rank==3;});
    chat0.SetStanding({true, 3});
    Wait([&]{const auto rows=chat1.Get().standings;return rows.size()==2&&rows[0].car==1&&rows[0].rank==3&&rows[1].rank==3;});
    // Active native race with temporarily missing rank must stay connected,
    // submit valid unknown wire rank, and recover when native data returns.
    for(uint32_t missing : {0u,41u,0xffffffffu}) {
      chat0.SetStanding({true, missing});
      Wait([&]{const auto view=chat0.Get();return view.connected&&view.standings.size()==2&&view.standings.back().car==1&&view.standings.back().rank==0;});
      chat0.SetStanding({true, 3});
      Wait([&]{const auto view=chat0.Get();return view.connected&&view.standings.size()==2&&view.standings[0].rank==3;});
    }
    // Actual native sampler -> per-session lifecycle -> authenticated C++
    // worker/relay, including both Rally catalog entries and every result phase.
    ResultsStandingFixture mem0(game),mem1(game);
    Online::StandingLifecycle life0,life1;
    mem0.Rank(4);mem1.Rank(2);
    chat0.SetStanding(life0.Update(mem0.Sample()));chat1.SetStanding(life1.Update(mem1.Sample()));
    for(auto phase:mem0.ResultPhases()) {
      mem0.Results(phase);mem1.Results(phase);mem0.Rank(3);mem1.Rank(1);
      chat0.SetStanding(life0.Update(mem0.Sample()));chat1.SetStanding(life1.Update(mem1.Sample()));
      Wait([&]{auto v=chat0.Get();return v.connected&&v.standings.size()==2&&v.standings[0].car==2&&v.standings[0].rank==1&&v.standings[1].rank==3;});
      mem0.Rank(0);mem1.Rank(0);
      chat0.SetStanding(life0.Update(mem0.Sample()));chat1.SetStanding(life1.Update(mem1.Sample()));
      assert(chat0.Get().connected&&chat1.Get().connected);
    }
    mem0.Race();mem0.Rank(0);chat0.SetStanding(life0.Update(mem0.Sample()));
    Wait([&]{auto v=chat1.Get();return v.connected&&v.standings.size()==2&&v.standings.back().car==1&&v.standings.back().rank==0;});
    mem0.Inactive();life0.Update(mem0.Sample());
    mem0.Results(mem0.ResultPhases()[0]);assert(!life0.Update(mem0.Sample()).Visible());
    std::cout<<"PASS authenticated native results/final-sort/retention/new-race clearing: "<<Online::Games[game]<<std::endl;
    chat0.SetStanding({false, 0});
    Wait([&]{const auto rows=chat1.Get().standings;return rows.size()==2&&rows[0].car==2&&rows[1].rank==0;});
    std::cout<<"PASS native standing exchange, overtake, identity, tie, race reset: "<<Online::Games[game]<<std::endl;

    auto s0=std::make_shared<CNetServer>(0,"Online test master",false),s1=std::make_shared<CNetServer>(0,"Online test slave",false);
    assert(s0->Start()&&s1->Start());
    CNetBoardLink l0(s0,"127.0.0.1",port,true,guids[game],a.ticket),l1(s1,"127.0.0.1",port,true,guids[game],b.ticket);
    // Slave opens its actual two outbound channels first; no premature assignment.
    assert(l1.Connect());assert(l1.RelayIndex()<0);assert(l0.Connect());
    Wait([&]{return l0.RelayIndex()>=0&&l1.RelayIndex()>=0;});
    assert(l0.RelayIndex()==0&&l1.RelayIndex()==1&&l0.RelayCount()==2&&l1.RelayCount()==2);
    chat1.SendChat("¡Buena suerte en pista!");
    Wait([&]{return chat0.Get().messages.back().text=="¡Buena suerte en pista!"&&chat0.Get().messages.back().car==2;});
    std::vector<uint8_t> payload(sizes[game],0x37),received;
    l0.Send(payload.data(),payload.size());Wait([&]{return l1.TryReceive(received)==CNetBoardLink::ReceiveResult::Data;});assert(received==payload);
    l1.Send(payload.data(),payload.size());Wait([&]{return l0.TryReceive(received)==CNetBoardLink::ReceiveResult::Data;});assert(received==payload);
    chat0.Disconnect();chat1.Disconnect();
    std::cout<<"PASS C++ GUI client + lobby/in-game chat -> ticket -> production two-channel link: "<<Online::Games[game]<<std::endl;
  }
  for (unsigned game = 0; game < 2; ++game) {
    constexpr unsigned count = 16;
    std::vector<std::unique_ptr<Online::LobbyClient>> players;
    for (unsigned i = 0; i <= count; ++i) {
      players.emplace_back(new Online::LobbyClient);
      players.back()->Connect(endpoint, "Cab " + std::to_string(i+1), Online::GameMaskAll, true);
    }
    Wait([&]{for(auto& p:players) if(!p->Get().connected) return false;return true;});
    players[0]->Create(game,"16-cab regression");
    Wait([&]{return players[0]->Get().room != 0;});
    const auto room = players[0]->Get().room;
    for (unsigned i=1;i<count;++i) {
      players[i]->Join(room);
      Wait([&]{return players[i]->Get().room==room;});
    }
    Wait([&]{return players[0]->Get().rooms.back().members.size()==count;});
    players[count]->Join(room);
    Wait([&]{return !players[count]->Get().error.empty();});
    assert(players[count]->Get().room==0);
    players[0]->Start(room);
    Wait([&]{for(unsigned i=0;i<count;++i) if(!players[i]->Get().launch) return false;return true;});
    std::vector<std::shared_ptr<CNetServer>> servers;
    std::vector<std::unique_ptr<CNetBoardLink>> links;
    std::vector<std::unique_ptr<Online::RoomChatClient>> rankChats;
    for (unsigned i=0;i<count;++i) {
      auto launch=players[i]->Get();
      assert(launch.index==i&&launch.count==count);
      players[i]->Disconnect();
      rankChats.emplace_back(new Online::RoomChatClient);
      rankChats.back()->Connect(endpoint,launch.ticket,"spoofed fallback");
      rankChats.back()->SetStanding({true, count-i});
      auto server=std::make_shared<CNetServer>(0,"16-cab link",false);
      assert(server->Start());servers.push_back(server);
      links.emplace_back(new CNetBoardLink(server,"127.0.0.1",port,true,guids[game],launch.ticket));
      assert(links.back()->Connect());
    }
    Wait([&]{for(auto& l:links) if(l->RelayIndex()<0) return false;return true;});
    for(unsigned i=0;i<count;++i) assert(links[i]->RelayIndex()==int(i)&&links[i]->RelayCount()==count);
    Wait([&]{for(auto& chat:rankChats) {
      auto view=chat->Get();if(!view.connected||view.standings.size()!=count) return false;
      for(unsigned i=0;i<count;++i) if(view.standings[i].car!=count-i||view.standings[i].rank!=i+1||view.standings[i].name!="Cab "+std::to_string(count-i)) return false;
    }return true;});
    assert(rankChats.back()->Get().car==16&&rankChats.back()->Get().count==16);
    std::vector<uint8_t> payload(848,0xA5),received;
    links[15]->Send(payload.data(),payload.size());
    Wait([&]{return links[0]->TryReceive(received)==CNetBoardLink::ReceiveResult::Data;});
    assert(received==payload);
    // This is the master Return-to-lobby action: tear down board channels.
    links[0].reset();
    Wait([&]{for(unsigned i=1;i<count;++i) if(links[i]->RelayIndex()!=-2) return false;return true;});
    rankChats.clear();links.clear();servers.clear();
    Online::LobbyClient returned;
    returned.Connect(endpoint,"Returned player",Online::GameMaskAll,true);
    Wait([&]{return returned.Get().connected;});
    assert(returned.Get().room==0&&!returned.Get().launch&&returned.Get().ticket.empty());
    std::cout<<"PASS 16 cabinets + car16 chat + 17th rejection + master teardown notifies all + fresh lobby: "<<Online::Games[game]<<std::endl;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  std::cout<<"PASS ticket parser, nonblocking cancellation, creator order, all seven Online entries"<<std::endl;
}
