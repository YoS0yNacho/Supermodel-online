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
 // sourceUs is the origin's own monotonic stamp (ordering only). When timed, ageUs
 // is the measured transit age at receivedUs and uncertaintyUs bounds its error.
 struct Sample { std::vector<uint8_t> raw;uint64_t sourceUs=0,receivedUs=0,ageUs=0,uncertaintyUs=0,controlUs=0;uint16_t heard=0;bool timed=false; };
 static constexpr uint64_t MaxAgeUs=2000000;
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
 // freshUs: how long a peer may stay quiet before it stops counting as heard.
 void Submit(const void* bytes,size_t size,uint64_t sourceUs,uint64_t freshUs,uint64_t controlUs=0) {
  if(size!=m_size || !sourceUs || !Active()) return;
  const auto* p=static_cast<const uint8_t*>(bytes);
  std::lock_guard<std::mutex> l(m_mutex);
  m_pending.assign(p,p+size);m_pendingSourceUs=sourceUs;m_pendingControlUs=controlUs;m_freshUs=freshUs;
 }
 // Returns only a sample strictly newer than afterSourceUs (no copy otherwise).
 bool Latest(unsigned origin,Sample& sample,uint64_t afterSourceUs=0) {
  std::lock_guard<std::mutex> l(m_mutex);
  if(origin>=m_count || origin==m_index || m_samples[origin].raw.empty() || m_samples[origin].sourceUs<=afterSourceUs) return false;
  sample=m_samples[origin];return true;
 }
 // True while every other cabinet is sending usable timed snapshots AND reports
 // that it is receiving ours. Only then may the reliable ring stay silent.
 bool PrimaryReady() {
  if(!Active()) return false;
  const auto now=Now();
  std::lock_guard<std::mutex> l(m_mutex);
  for(unsigned i=0;i<m_count;++i) {
   if(i==m_index) continue;
   const auto& s=m_samples[i];
   if(!Fresh(s,now) || !(s.heard&(1u<<m_index))) return false;
  }
  return true;
 }
private:
 struct ClockSample { uint64_t rttUs=0,at=0;int64_t offset=0; };
 struct Clock { std::array<ClockSample,8> samples{};unsigned next=0; };
 bool Fresh(const Sample& s,uint64_t now) const { return s.timed && !s.raw.empty() && now>=s.receivedUs && now-s.receivedUs<=m_freshUs; }
 // Lowest-RTT estimate of (responder clock - local clock) from the last few Acks.
 static bool Estimate(const Clock& c,uint64_t now,int64_t& offset,uint64_t& rttUs) {
  bool found=false;
  for(const auto& s:c.samples) if(s.at && now>=s.at && now-s.at<=10000000 && (!found || s.rttUs<rttUs)) { offset=s.offset;rttUs=s.rttUs;found=true; }
  return found;
 }
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
     if(h.origin==m_index && h.sequence==expected.first && h.sourceUs==expected.second && ackNow>=h.sourceUs && ackNow-h.sourceUs<=2000000) {
      m_lastAck=ackNow;if(direct) hostAck=ackNow;
      uint64_t remote=0;std::memcpy(&remote,rx->data+sizeof(h),RelayUdp::AckClockSize);
      if(remote && remote<=uint64_t(INT64_MAX)) {
       const uint64_t rtt=ackNow-h.sourceUs;
       std::lock_guard<std::mutex> l(m_mutex);
       auto& c=m_clocks[direct?1:0];
       c.samples[c.next++%c.samples.size()]={rtt,ackNow,int64_t(remote)-int64_t(h.sourceUs+rtt/2)};
      }
     }
     continue;
    }
    if(h.kind!=RelayUdp::Snapshot || h.origin>=m_count || h.origin==m_index) continue;
    std::vector<uint8_t> encoded,raw;
    const auto received=Now();
    if(!m_assemblies[h.origin].Add(h,rx->data+sizeof(h),received,encoded) || !RelayUdp::Decode(encoded,m_size+RelayUdp::TrailerSize,raw)) continue;
    RelayUdp::Trailer t;std::memcpy(&t,raw.data()+m_size,sizeof(t));raw.resize(m_size);
    std::lock_guard<std::mutex> l(m_mutex);
    auto& s=m_samples[h.origin];
    if(h.sourceUs<=s.sourceUs) continue;
    s.raw=std::move(raw);s.sourceUs=h.sourceUs;s.receivedUs=received;s.heard=t.heard;s.controlUs=t.controlUs;s.timed=false;s.ageUs=s.uncertaintyUs=0;
    // Age = (receive time on the shared clock) - (sample time on the shared clock).
    int64_t offset=0;uint64_t rtt=0;
    if((t.clock==1 || t.clock==2) && t.clockUs && t.clockUs<=uint64_t(INT64_MAX) && Estimate(m_clocks[t.clock-1],received,offset,rtt)) {
     const int64_t age=int64_t(received)+offset-int64_t(t.clockUs);
     s.uncertaintyUs=rtt/2+uint64_t(t.rttUs)/2;
     // Tolerate a negative age within the measurement error; reject implausible clocks.
     if(age>=-int64_t(s.uncertaintyUs)-2000 && age<=int64_t(MaxAgeUs)) { s.ageUs=uint64_t(std::max<int64_t>(0,age));s.timed=true; }
    }
   }
   std::vector<uint8_t> pending;uint64_t source=0,control=0;
   { std::lock_guard<std::mutex> l(m_mutex);pending.swap(m_pending);source=m_pendingSourceUs;control=m_pendingControlUs; }
   const auto routeNow=Now();
   const bool useHost=m_hosted && hostRoute && routeAt && routeNow>=routeAt && routeNow-routeAt<1000000 && hostAck && routeNow>=hostAck && routeNow-hostAck<1500000;
   m_usingHost=useHost;
   if(m_hosted && useHost!=lastRoute) {std::fprintf(stderr,"UDP self-host: snapshots via %s.\n",useHost?"creator":"central relay fallback");lastRoute=useHost;}
   if(pending.size()==m_size && Active()) {
    RelayUdp::Trailer t;t.controlUs=control;
    {
     std::lock_guard<std::mutex> l(m_mutex);
     for(unsigned i=0;i<m_count;++i) if(i!=m_index && Fresh(m_samples[i],routeNow)) t.heard|=uint16_t(1u<<i);
     int64_t offset=0;uint64_t rtt=0;
     if(Estimate(m_clocks[useHost?1:0],routeNow,offset,rtt) && int64_t(source)+offset>0) {
      t.clock=useHost?2:1;t.rttUs=uint32_t(std::min<uint64_t>(rtt,UINT32_MAX));t.clockUs=uint64_t(int64_t(source)+offset);
     }
    }
    pending.resize(pending.size()+sizeof(t));std::memcpy(pending.data()+m_size,&t,sizeof(t));
    pending=RelayUdp::Encode(pending.data(),pending.size());
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
 std::vector<uint8_t> m_pending;uint64_t m_pendingSourceUs=0,m_pendingControlUs=0,m_freshUs=300000;
 std::array<Clock,2> m_clocks{}; // 0 = central relay, 1 = creator host
};
#endif
