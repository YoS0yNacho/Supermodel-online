// SDL_net 2 outbound UDP client; header-only to share the existing netboard
// test/build source lists. Worker never reads or writes emulated RAM.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_RELAY_UDP_CLIENT_H
#define INCLUDED_RELAY_UDP_CLIENT_H
#include "RelayUpnp.h"
#include "RelayUdpHost.h"
#include "SDLIncludes.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <string>
#include <utility>
class CRelayUdpClient {
public:
 struct Sample { std::vector<uint8_t> raw;uint64_t sourceUs=0,receivedUs=0; };
 static uint64_t Now() { return uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()); }
 CRelayUdpClient(const std::string& host,uint16_t port,const uint8_t* token,unsigned index,unsigned count,size_t size,const RelayUdp::HostConfig* hosted=nullptr)
  :m_index(index),m_count(count),m_size(size),m_samples(count),m_assemblies(count) {
  std::memcpy(m_token.data(),token,16);
  if(SDLNet_ResolveHost(&m_server,host.c_str(),port)<0) return;
  if(hosted) { m_hosted=true;m_hostConfig=*hosted;m_hostAddress.host=hosted->ip;m_hostAddress.port=SDL_SwapBE16(hosted->port);if(index==0) SDLNet_ResolveHost(&m_hostAddress,"127.0.0.1",hosted->port); }
  m_socket=SDLNet_UDP_Open(0);
  if(!m_socket) return;
  m_thread=std::thread([this]{ Run(); });
 }
 ~CRelayUdpClient() { m_stop=true;if(m_thread.joinable()) m_thread.join();if(m_socket) SDLNet_UDP_Close(m_socket); }
 bool SelfHostedActive() const { return m_usingHost.load(); }
 bool Available() const { return m_socket!=nullptr; }
 unsigned Index() const { return m_index; }
 unsigned Count() const { return m_count; }
 bool Active() const { const auto last=m_lastAck.load();const auto now=Now();return last && now>=last && now-last<2000000; }
 void Submit(const void* bytes,size_t size) {
  if(size!=m_size || !Active()) return;
  auto encoded=RelayUdp::Encode(bytes,size);
  if(encoded.empty()) return;
  std::lock_guard<std::mutex> l(m_mutex);
  m_pending=std::move(encoded);m_pendingSourceUs=Now();
 }
 bool Latest(unsigned origin,Sample& sample) {
  std::lock_guard<std::mutex> l(m_mutex);
  if(origin>=m_count || origin==m_index || m_samples[origin].raw.empty()) return false;
  sample=m_samples[origin];return true;
 }
private:
 void Run() {
  UDPpacket* tx=SDLNet_AllocPacket(int(RelayUdp::DatagramSize));
  UDPpacket* rx=SDLNet_AllocPacket(int(RelayUdp::DatagramSize+1));
  if(!tx || !rx) { if(tx) SDLNet_FreePacket(tx);if(rx) SDLNet_FreePacket(rx);return; }
  std::unique_ptr<RelayUdp::Host> host;
  std::unique_ptr<RelayUpnp::Mapping> mapping;
  if(m_hosted && m_index==0) {
   host=std::make_unique<RelayUdp::Host>(m_hostConfig);
   if(host->Available()) {
    std::string label="Supermodel-"+std::to_string(Now());
    mapping=std::make_unique<RelayUpnp::Mapping>(m_hostConfig.port,label);
    std::fprintf(stderr,"UDP self-host: listening on UDP %u; reliable game traffic stays central TCP.\n",unsigned(m_hostConfig.port));
   } else std::fprintf(stderr,"UDP self-host: cannot bind port %u; central UDP fallback retained.\n",unsigned(m_hostConfig.port));
  }
  auto send=[&](RelayUdp::Header& h,const uint8_t* p,size_t n,const IPaddress& address) {
   std::memcpy(tx->data,&h,sizeof(h));if(n) std::memcpy(tx->data+sizeof(h),p,n);
   tx->len=int(sizeof(h)+n);tx->address=address;SDLNet_UDP_Send(m_socket,-1,tx);
  };
  uint64_t nextHello=0,hostAck=0,routeAt=0;bool hostRoute=false,lastRoute=false,haveRoute=false;uint32_t seq=0,helloSeq=0,routeSeq=0;
  std::array<std::pair<uint32_t,uint64_t>,8> helloWindow{};
  while(!m_stop.load()) {
   const auto now=Now();
   if(host) host->Poll(rx,tx,now);
   if(now>=nextHello) {
    RelayUdp::Header h;std::memcpy(h.token,m_token.data(),16);h.origin=uint16_t(m_index);
    h.sequence=++helloSeq;h.sourceUs=now;helloWindow[helloSeq%helloWindow.size()]={helloSeq,now};send(h,nullptr,0,m_server);
    if(m_hosted) {
     send(h,nullptr,0,m_hostAddress);
     h.kind=RelayUdp::HostStatus;h.sequence=hostAck && now-hostAck<1000000?1:0;send(h,nullptr,0,m_server);
    }
    nextHello=now+500000;
   }
   for(unsigned budget=0;budget<128 && SDLNet_UDP_Recv(m_socket,rx)>0;++budget) {
    RelayUdp::Header h;
    const bool central=rx->address.host==m_server.host && rx->address.port==m_server.port;
    const bool direct=m_hosted && rx->address.host==m_hostAddress.host && rx->address.port==m_hostAddress.port;
    if((!central && !direct) || !RelayUdp::Parse(rx->data,size_t(rx->len),h) || std::memcmp(h.token,m_token.data(),16)) continue;
    if(h.kind==RelayUdp::Route) {
     if(central && m_hosted && h.origin==m_index && h.sourceUs<=1 && (!haveRoute || RelayUdp::Newer(h.sequence,routeSeq))) {hostRoute=h.sourceUs!=0;routeAt=Now();routeSeq=h.sequence;haveRoute=true;}
     continue;
    }
    if(h.kind==RelayUdp::Ack) {
     const auto expected=helloWindow[h.sequence%helloWindow.size()];
     const auto ackNow=Now();
     if(h.origin==m_index && h.sequence==expected.first && h.sourceUs==expected.second && ackNow>=h.sourceUs && ackNow-h.sourceUs<=2000000) { m_lastAck=ackNow;if(direct) hostAck=ackNow; }
     continue;
    }
    if(h.kind!=RelayUdp::Snapshot || h.origin>=m_count || h.origin==m_index) continue;
    std::vector<uint8_t> encoded,raw;
    const auto received=Now();
    if(!m_assemblies[h.origin].Add(h,rx->data+sizeof(h),received,encoded) || !RelayUdp::Decode(encoded,m_size,raw)) continue;
    std::lock_guard<std::mutex> l(m_mutex);
    auto& s=m_samples[h.origin];
    if(h.sourceUs<=s.sourceUs) continue;
    s.raw=std::move(raw);s.sourceUs=h.sourceUs;s.receivedUs=received;
   }
   std::vector<uint8_t> pending;uint64_t source=0;
   { std::lock_guard<std::mutex> l(m_mutex);pending.swap(m_pending);source=m_pendingSourceUs; }
   const auto routeNow=Now();
   const bool useHost=m_hosted && hostRoute && routeAt && routeNow>=routeAt && routeNow-routeAt<1000000 && hostAck && routeNow>=hostAck && routeNow-hostAck<1500000;
   m_usingHost=useHost;
   if(m_hosted && useHost!=lastRoute) {std::fprintf(stderr,"UDP self-host: snapshots via %s.\n",useHost?"creator":"central relay fallback");lastRoute=useHost;}
   if(!pending.empty() && Active()) {
    RelayUdp::Header h;h.kind=RelayUdp::Snapshot;h.origin=uint16_t(m_index);h.total=uint16_t(pending.size());
    h.parts=uint8_t((pending.size()+RelayUdp::ChunkSize-1)/RelayUdp::ChunkSize);h.sequence=++seq;h.sourceUs=source;
    std::memcpy(h.token,m_token.data(),16);
    for(h.part=0;h.part<h.parts;++h.part) send(h,pending.data()+h.part*RelayUdp::ChunkSize,std::min(RelayUdp::ChunkSize,pending.size()-h.part*RelayUdp::ChunkSize),useHost?m_hostAddress:m_server);
   }
   std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  SDLNet_FreePacket(tx);SDLNet_FreePacket(rx);
 }
 const unsigned m_index,m_count;const size_t m_size;
 bool m_hosted=false;RelayUdp::HostConfig m_hostConfig;IPaddress m_hostAddress{};
 UDPsocket m_socket=nullptr;IPaddress m_server{};std::array<uint8_t,16> m_token{};
 std::atomic<bool> m_stop{false},m_usingHost{false};std::atomic<uint64_t> m_lastAck{0};std::thread m_thread;
 std::mutex m_mutex;std::vector<Sample> m_samples;std::vector<RelayUdp::Assembly> m_assemblies;
 std::vector<uint8_t> m_pending;uint64_t m_pendingSourceUs=0;
};
#endif
