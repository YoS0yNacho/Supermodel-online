// Native player-hosted TCP ring + UDP presentation service. No SDL/Python lifetime dependency.
// r18 can own its room/chat/roster too; central then supplies discovery only.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_ONLINE_PLAYER_HOST_H
#define INCLUDED_ONLINE_PLAYER_HOST_H
#include "Network/RelayUpnp.h"
#include "Network/RelayUdpWire.h"
#include "PlayerRoom.h"
#include "RoomDirectory.h"
#ifndef _WIN32
#include <netinet/tcp.h>
#endif
#include <array>
#include <cstring>
#include <mutex>
#include <vector>
namespace Online {
class PlayerHost {
public:
 using Member=PlayerRoom::Member;
 struct Config { unsigned game=0,tick=20;std::vector<Member> members; };
 static uint32_t Read(const uint8_t* p){uint32_t n;std::memcpy(&n,p,4);return n;}
 static void Put(std::vector<uint8_t>& b,uint32_t n){auto s=b.size();b.resize(s+4);std::memcpy(b.data()+s,&n,4);}
 static std::vector<uint8_t> Frame(const char* id,const std::vector<uint8_t>& body={}){std::vector<uint8_t> b;Put(b,uint32_t(body.size()+8));b.insert(b.end(),id,id+4);b.insert(b.end(),body.begin(),body.end());return b;}
 PlayerHost(uint16_t port,const std::array<uint8_t,16>& probe,bool upnp=true):port(port),probe(probe),worker([this,upnp]{Run(upnp);finished=true;}){}
 ~PlayerHost(){Stop();if(worker.joinable())worker.join();}
 PlayerHost(const PlayerHost&)=delete;PlayerHost& operator=(const PlayerHost&)=delete;
 void Stop(){stop=true;}
 bool Booted()const{return booted;}
 bool Finished()const{return finished;}
 bool Bound()const{return bound;}
 bool Armed()const{return armed;}
 unsigned Status()const{return failed?3u:started?2u:1u;}
 bool Configure(const Config& c){
  if(failed || stop || !bound || c.game>=7 || c.tick<5 || c.tick>60 || c.members.size()<2 || c.members.size()>Capacity(c.game))return false;
  for(size_t i=0;i<c.members.size();++i){bool a=false,b=false;for(unsigned j=0;j<16;++j){a|=c.members[i].ticket[j]!=0;b|=c.members[i].cookie[j]!=0;}if(!a||!b)return false;
   for(size_t j=0;j<i;++j)if(c.members[i].ticket==c.members[j].ticket || c.members[i].cookie==c.members[j].cookie)return false;
  }
  std::lock_guard<std::mutex> l(mutex);if(configReady)return false;config=c;configReady=true;return true;
 }
 bool HostRoom(const PlayerRoom::Settings& value){
  std::lock_guard<std::mutex> l(mutex);if(roomReady||configReady||!bound||failed||stop||value.game>=7||value.tick<5||value.tick>60||!value.id||value.port!=port)return false;
  roomSettings=value;roomReady=true;return true;
 }
 static unsigned Capacity(unsigned game){return game<2?16:game==4?Srally2Research::StockCapacity:game==5?6:8;}
 static uint64_t Guid(unsigned game){static const uint64_t g[]={0x5bf177da34878,0x5bf177da34875,0x5bf177da34876,0x5bf177da34877,Srally2Research::StockGuid,0x5bf177da3487a,Srally2Research::Guid};return game<7?g[game]:0;}
 static size_t Segment(unsigned game){static const size_t s[]={848,848,1024,1024,2048,3264,2048};return game<7?s[game]:0;}
private:
 using Socket=RelayUpnp::Socket;static constexpr Socket Bad=RelayUpnp::Bad;
 using Clock=std::chrono::steady_clock;
 static uint64_t Now(){return uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch()).count());}
 struct Conn {
  Socket fd=Bad;uint32_t ip=0,id=0;uint64_t activity=Now();int index=-1,channel=-1;bool replyOnly=false,rejected=false;
  uint64_t born=Now(),progress=born;std::vector<uint8_t> input,output,hello;size_t sent=0;
  ~Conn(){if(fd!=Bad)RelayUpnp::Close(fd);}
  size_t Queued()const{return output.size()-sent;}
  bool Queue(const std::vector<uint8_t>& b){if(Queued()+b.size()>524288)return false;if(!Queued())progress=Now();if(sent){output.erase(output.begin(),output.begin()+std::ptrdiff_t(sent));sent=0;}output.insert(output.end(),b.begin(),b.end());return true;}
 };
 struct Peer {
  Member auth;Conn* channels[2]={};std::array<uint8_t,16> nonce{};
  uint32_t ip=0;sockaddr_in endpoint{};uint64_t hello=0,creditAt=0;double credit=0;
  RelayUdp::Header latest{};bool seen=false;uint8_t mask=0;
 };
 uint16_t port;std::array<uint8_t,16> probe;
 std::atomic<bool> stop{false},booted{false},bound{false},armed{false},started{false},failed{false},finished{false};
 std::mutex mutex;Config config;bool configReady=false;
 PlayerRoom::Settings roomSettings;bool roomReady=false;std::unique_ptr<PlayerRoom> room;uint32_t nextConnection=1;
 std::vector<Peer> peers;std::vector<std::unique_ptr<Conn>> connections;
 unsigned tick=20;size_t segment=0;uint64_t guid=0,armTime=0;Socket tcp=Bad,udp=Bad;std::thread worker;
 static bool Nonzero(const uint8_t* p,size_t n){for(size_t i=0;i<n;++i)if(p[i])return true;return false;}
 Conn* Target(const Conn& c){if(c.index<0)return nullptr;size_t n=peers.size(),i=size_t(c.index);return c.channel==0?peers[(i+1)%n].channels[1]:peers[(i+n-1)%n].channels[0];}
 void Fail(){failed=true;for(auto& c:connections){if(c->fd!=Bad){RelayUpnp::Close(c->fd);c->fd=Bad;}}for(auto& p:peers){p.hello=0;p.channels[0]=p.channels[1]=nullptr;}}
 bool Message(Conn& c,const std::vector<uint8_t>& packet){
  const auto id=Read(packet.data()+4);const size_t n=packet.size();
  if(c.hello.empty()){
   if(id==0x31514850 && n==24 && !std::memcmp(packet.data()+8,probe.data(),16)){
    auto response=packet;std::memcpy(response.data()+4,"PHA1",4);c.replyOnly=true;return c.Queue(response);
   }
   if(id!=0x314c4548 || n>1032)return false;
   c.hello=packet;return true;
  }
  if(c.index<0 && room && (room->Registered(c.id) || id==Srally2Research::LobbyHelloId || id==0x31434c52))return room->Message(c.id,c.ip,packet);
  if(c.index<0){
   if(!armed || started || failed || id!=0x334a4c52 || n!=56 || Read(packet.data()+8)!=1 || Read(packet.data()+12)>1 || !Nonzero(packet.data()+16,16))return false;
   uint64_t g=0;std::memcpy(&g,packet.data()+32,8);if(g!=guid)return false;
   for(size_t i=0;i<peers.size();++i){auto& p=peers[i];if(std::memcmp(packet.data()+40,p.auth.ticket.data(),16))continue;
    unsigned ch=Read(packet.data()+12);if(p.channels[ch] || (p.channels[ch^1] && (p.ip!=c.ip || std::memcmp(p.nonce.data(),packet.data()+16,16))))return false;
    c.index=int(i);c.channel=int(ch);p.channels[ch]=&c;p.ip=c.ip;std::memcpy(p.nonce.data(),packet.data()+16,16);return true;
   }return false;
  }
  if(!started || failed)return false;
  const size_t body=n-8;
  bool valid=(id==0x314e4950 && body==8)||(id==0x314e4f50 && body==16)||
   (id==0x3144424e && (body==0||body==1||body==2||body==8||body==48||body==56||body==segment||body==segment+48));
  auto target=Target(c);return valid && target && target->Queue(packet);
 }
 void StartIfReady(){
  if(!armed || started || failed)return;
  for(auto& p:peers)if(!p.channels[0]||!p.channels[1])return;
  for(size_t i=0;i<peers.size();++i)for(unsigned ch=0;ch<2;++ch){auto& c=*peers[i].channels[ch];auto target=Target(c);if(!target || !c.Queue(target->hello)){Fail();return;}
   std::vector<uint8_t> b;Put(b,1);Put(b,tick);if(!c.Queue(Frame("RLT1",b))){Fail();return;}
   if(ch==0){b.clear();Put(b,1);Put(b,port);Put(b,uint32_t(i));Put(b,uint32_t(peers.size()));b.insert(b.end(),peers[i].auth.cookie.begin(),peers[i].auth.cookie.end());if(!c.Queue(Frame("RLU1",b))){Fail();return;}}
   b.clear();Put(b,1);Put(b,uint32_t(i));Put(b,uint32_t(peers.size()));Put(b,ch);if(!c.Queue(Frame("RLA1",b))){Fail();return;}
  }started=true;std::fprintf(stderr,"Player host: TCP ring and UDP gameplay started for %u cabinets; central server carries no gameplay.\n",unsigned(peers.size()));
 }
 void PollUdp(){
  for(unsigned budget=0;budget<128;++budget){uint8_t bytes[1201];sockaddr_in address{};
#ifdef _WIN32
   int length=sizeof(address);
#else
   socklen_t length=sizeof(address);
#endif
   int n=recvfrom(udp,reinterpret_cast<char*>(bytes),sizeof(bytes),0,reinterpret_cast<sockaddr*>(&address),&length);if(n<=0)break;
   if(n==24 && Read(bytes)==24 && !std::memcmp(bytes+4,"PHQ1",4) && !std::memcmp(bytes+8,probe.data(),16)){
    std::memcpy(bytes+4,"PHA1",4);sendto(udp,reinterpret_cast<char*>(bytes),24,0,reinterpret_cast<sockaddr*>(&address),sizeof(address));continue;
   }
   RelayUdp::Header h;if(!started || failed || !RelayUdp::Parse(bytes,size_t(n),h) || h.origin>=peers.size())continue;
   auto& s=peers[h.origin];if(std::memcmp(h.token,s.auth.cookie.data(),16) || address.sin_addr.s_addr!=s.ip)continue;
   auto now=Now();if(h.kind==RelayUdp::Hello){if(s.hello && now-s.hello<100000)continue;s.endpoint=address;s.hello=now;h.kind=RelayUdp::Ack;std::memcpy(bytes,&h,sizeof(h));sendto(udp,reinterpret_cast<char*>(bytes),int(sizeof(h)),0,reinterpret_cast<sockaddr*>(&address),sizeof(address));continue;}
   if(h.kind!=RelayUdp::Snapshot || !s.hello || now-s.hello>3000000 || address.sin_port!=s.endpoint.sin_port)continue;
   const double rate=double(RelayUdp::MaxEncodedSize+3*RelayUdp::HeaderSize)*tick,burst=rate/4+RelayUdp::MaxEncodedSize;
   s.credit=std::min(burst,s.credit+(s.creditAt?double(now-s.creditAt)/1000000*rate:0));s.creditAt=now;if(s.credit<n)continue;s.credit-=n;
   if(s.seen && h.sequence!=s.latest.sequence && !RelayUdp::Newer(h.sequence,s.latest.sequence))continue;
   if(!s.seen || h.sequence!=s.latest.sequence){s.latest=h;s.seen=true;s.mask=0;}
   if(h.sourceUs!=s.latest.sourceUs || h.total!=s.latest.total || h.parts!=s.latest.parts || s.mask&(1u<<h.part))continue;
   s.mask|=uint8_t(1u<<h.part);
   for(size_t i=0;i<peers.size();++i){auto& dest=peers[i];if(i==h.origin || !dest.hello || now-dest.hello>3000000)continue;std::memcpy(h.token,dest.auth.cookie.data(),16);std::memcpy(bytes,&h,sizeof(h));sendto(udp,reinterpret_cast<char*>(bytes),n,0,reinterpret_cast<sockaddr*>(&dest.endpoint),sizeof(dest.endpoint));}
  }
 }
 void Run(bool upnp){
#ifdef _WIN32
  WSADATA data;if(WSAStartup(MAKEWORD(2,2),&data)){failed=true;booted=true;return;}struct Cleanup{~Cleanup(){WSACleanup();}}cleanup;
#endif
  RelayUpnp::Owner tcpOwner,udpOwner;tcpOwner.fd=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);udpOwner.fd=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);tcp=tcpOwner.fd;udp=udpOwner.fd;
#ifndef _WIN32
  if(tcp!=Bad && tcp>=FD_SETSIZE){failed=true;booted=true;return;}
#endif
  sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_port=htons(port);addr.sin_addr.s_addr=INADDR_ANY;
  if(tcp==Bad || udp==Bad || !RelayUpnp::Nonblock(tcp) || !RelayUpnp::Nonblock(udp) || bind(tcp,reinterpret_cast<sockaddr*>(&addr),sizeof(addr)) || bind(udp,reinterpret_cast<sockaddr*>(&addr),sizeof(addr)) || listen(tcp,32)) {failed=true;booted=true;return;}
  bound=true;booted=true;
  std::unique_ptr<RelayUpnp::Mapping> mappings;
  if(upnp){auto label="Supermodel-"+std::to_string(Now());std::fprintf(stderr,"Player host: listening on TCP+UDP %u; requesting paired UPnP mappings.\n",unsigned(port));mappings=std::make_unique<RelayUpnp::Mapping>(port,label,"BOTH");}
  std::unique_ptr<RoomDirectory> directory;
  while(!stop && !failed){
   {std::lock_guard<std::mutex> l(mutex);if(roomReady&&!room){
    room=std::make_unique<PlayerRoom>(roomSettings,[this](uint32_t id,const std::vector<uint8_t>& bytes){for(auto& c:connections)if(c->id==id&&c->fd!=Bad){bool good=c->Queue(bytes);if(!good)c->rejected=true;return good;}return false;},[this](unsigned game,unsigned tick,const std::vector<Member>& members){Config c;c.game=game;c.tick=tick;c.members=members;return Configure(c);});
    directory=std::make_unique<RoomDirectory>(roomSettings,room->listedCount,room->listedState);
   }}
   if(room){room->Pump(armed,started,failed);if(room->Closed()){Fail();break;}}
   if(!armed){std::lock_guard<std::mutex> l(mutex);if(configReady){tick=config.tick;segment=Segment(config.game);guid=Guid(config.game);for(auto& member:config.members){Peer p;p.auth=member;p.credit=double(RelayUdp::MaxEncodedSize+3*RelayUdp::HeaderSize)*tick/4+RelayUdp::MaxEncodedSize;peers.push_back(p);}armTime=Now();armed=true;}}
   PollUdp();StartIfReady();
   fd_set read,write;FD_ZERO(&read);FD_ZERO(&write);FD_SET(tcp,&read);Socket largest=tcp;
   for(auto& p:connections){auto& c=*p;if(c.fd==Bad)continue;auto target=started?Target(c):nullptr;
    if(!c.replyOnly && c.input.size()<49152 && (!target || target->Queued()<262144))FD_SET(c.fd,&read);
    if(c.Queued())FD_SET(c.fd,&write);
    if(c.fd>largest)largest=c.fd;
   }
   timeval tv{0,2000};int selected=select(int(largest+1),&read,&write,nullptr,&tv);if(selected<0){if(RelayUpnp::Again())continue;Fail();break;}
   if(FD_ISSET(tcp,&read))for(unsigned budget=0;budget<8;++budget){sockaddr_in peer{};
#ifdef _WIN32
    int length=sizeof(peer);
#else
    socklen_t length=sizeof(peer);
#endif
    Socket fd=accept(tcp,reinterpret_cast<sockaddr*>(&peer),&length);if(fd==Bad)break;
#ifndef _WIN32
    if(fd>=FD_SETSIZE){RelayUpnp::Close(fd);continue;}
#endif
    if(connections.size()>=60 || !RelayUpnp::Nonblock(fd)){RelayUpnp::Close(fd);continue;}
    int yes=1;
#ifdef _WIN32
    setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,reinterpret_cast<char*>(&yes),sizeof(yes));
#else
    setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&yes,sizeof(yes));
#endif
    auto c=std::make_unique<Conn>();c->fd=fd;c->ip=peer.sin_addr.s_addr;c->id=nextConnection++;connections.push_back(std::move(c));
   }
   for(auto& p:connections){auto& c=*p;if(c.fd==Bad)continue;bool good=!c.rejected;
    if(good && FD_ISSET(c.fd,&write)){
#ifdef MSG_NOSIGNAL
     constexpr int flags=MSG_NOSIGNAL;
#else
     constexpr int flags=0;
#endif
     int n=send(c.fd,reinterpret_cast<const char*>(c.output.data()+c.sent),int(c.Queued()),flags);if(n>0){c.sent+=size_t(n);c.progress=Now();if(!c.Queued()){c.output.clear();c.sent=0;}}else if(n==0 || !RelayUpnp::Again())good=false;
    }
    if(good && FD_ISSET(c.fd,&read)){uint8_t data[16384];int n=recv(c.fd,reinterpret_cast<char*>(data),sizeof(data),0);if(n>0){if(c.input.size()+size_t(n)>65536)good=false;else c.input.insert(c.input.end(),data,data+n);}else if(n==0 || !RelayUpnp::Again())good=false;}
    for(unsigned budget=0;good && budget<128 && c.input.size()>=8;++budget){auto target=started?Target(c):nullptr;if(target && target->Queued()>=262144)break;
     auto size=Read(c.input.data());if(size<8 || size>3320){good=false;break;}if(c.input.size()<size)break;
     std::vector<uint8_t> packet(c.input.begin(),c.input.begin()+size);c.input.erase(c.input.begin(),c.input.begin()+size);
     c.activity=Now();if(!Message(c,packet)){good=false;break;}if(c.replyOnly)break;
    }
    auto now=Now();bool control=room&&room->Registered(c.id);if((c.index<0 && (control?now-c.activity>40000000:now-c.born>10000000)) || (c.Queued() && now-c.progress>30000000) || (c.replyOnly && !c.Queued()))good=false;
    if(!good){if(room)room->Drop(c.id);if(c.index>=0){Fail();break;}RelayUpnp::Close(c.fd);c.fd=Bad;}
   }
   connections.erase(std::remove_if(connections.begin(),connections.end(),[](const auto& c){return c->fd==Bad;}),connections.end());
   if(armed && !started && Now()-armTime>90000000)Fail();
  }
  for(auto& c:connections){if(c->fd!=Bad){RelayUpnp::Close(c->fd);c->fd=Bad;}}connections.clear();
 }
};
}
#endif
