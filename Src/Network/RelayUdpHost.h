// Creator-hosted UDP presentation fanout only. Native game state stays TCP.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_RELAY_UDP_HOST_H
#define INCLUDED_RELAY_UDP_HOST_H
#include "RelayUdpWire.h"
#include "SDLIncludes.h"
#include <chrono>
namespace RelayUdp {
struct HostPeer { uint32_t ip=0; std::array<uint8_t,16> token{}; };
struct HostConfig { uint32_t ip=0; uint16_t port=0; unsigned tickRate=20; std::vector<HostPeer> peers; };
class Host {
 struct Peer {
  HostPeer auth; IPaddress endpoint{}; uint64_t hello=0,creditAt=0;
  double credit=0; Header latest{}; uint8_t mask=0; bool seen=false;
 };
 UDPsocket socket=nullptr; std::vector<Peer> peers; double rate=0,burst=0;
public:
 explicit Host(const HostConfig& config) {
  if(config.peers.size()<2 || config.peers.size()>16 || config.tickRate<5 || config.tickRate>60) return;
  socket=SDLNet_UDP_Open(config.port); if(!socket) return;
  rate=double(MaxEncodedSize+3*HeaderSize)*config.tickRate;burst=rate/4+MaxEncodedSize;
  for(auto& auth:config.peers) { Peer p;p.auth=auth;p.credit=burst;peers.push_back(p); }
  // The creator's own client reaches this socket over loopback, not hairpin NAT.
  IPaddress local{};SDLNet_ResolveHost(&local,"127.0.0.1",config.port);peers[0].auth.ip=local.host;
 }
 ~Host() { if(socket) SDLNet_UDP_Close(socket); }
 bool Available() const { return socket!=nullptr; }
 Host(const Host&)=delete;Host& operator=(const Host&)=delete;
 void Poll(UDPpacket* rx,UDPpacket* tx,uint64_t now) {
  if(!socket) return;
  for(unsigned budget=0;budget<128 && SDLNet_UDP_Recv(socket,rx)>0;++budget) {
   Header h;if(!Parse(rx->data,size_t(rx->len),h) || h.origin>=peers.size()) continue;
   auto& s=peers[h.origin];
   if(std::memcmp(h.token,s.auth.token.data(),16) || rx->address.host!=s.auth.ip) continue;
   if(h.kind==Hello) {
    if(s.hello && now-s.hello<100000) continue;
    s.endpoint=rx->address;s.hello=now;h.kind=Ack;
    // The Ack carries this host's clock so every cabinet can express sample times on it.
    const uint64_t clock=uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    std::memcpy(tx->data,&h,sizeof(h));std::memcpy(tx->data+sizeof(h),&clock,AckClockSize);
    tx->len=int(sizeof(h)+AckClockSize);tx->address=s.endpoint;SDLNet_UDP_Send(socket,-1,tx);continue;
   }
   if(h.kind!=Snapshot || !s.hello || now-s.hello>3000000 || rx->address.host!=s.endpoint.host || rx->address.port!=s.endpoint.port) continue;
   s.credit=std::min(burst,s.credit+(s.creditAt?double(now-s.creditAt)/1000000.0*rate:0));s.creditAt=now;
   if(s.credit<rx->len) continue;
   s.credit-=rx->len;
   if(s.seen && h.sequence!=s.latest.sequence && !Newer(h.sequence,s.latest.sequence)) continue;
   if(!s.seen || h.sequence!=s.latest.sequence) { s.latest=h;s.mask=0;s.seen=true; }
   if(h.sourceUs!=s.latest.sourceUs || h.total!=s.latest.total || h.parts!=s.latest.parts || (s.mask&(1u<<h.part))) continue;
   s.mask|=uint8_t(1u<<h.part);
   for(size_t i=0;i<peers.size();++i) {
    auto& dest=peers[i];if(i==h.origin || !dest.hello || now-dest.hello>3000000) continue;
    std::memcpy(h.token,dest.auth.token.data(),16);std::memcpy(tx->data,&h,sizeof(h));
    std::memcpy(tx->data+sizeof(h),rx->data+sizeof(h),size_t(rx->len)-sizeof(h));
    tx->len=rx->len;tx->address=dest.endpoint;SDLNet_UDP_Send(socket,-1,tx);
   }
  }
 }
};
}
#endif
