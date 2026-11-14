// SDL_net 2 outbound UDP client; header-only to share the existing netboard
// test/build source lists. Worker never reads or writes emulated RAM.
// Snapshots are sent by the submitting (emulation) thread itself, so a tick's
// data leaves immediately; the worker blocks in SDLNet_CheckSockets for
// incoming datagrams instead of polling, so arrivals are read immediately too.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_RELAY_UDP_CLIENT_H
#define INCLUDED_RELAY_UDP_CLIENT_H
#include "RelayUpnp.h"
#include "RelayUdpHost.h"
#include "Online/AdaptiveTickRate.h"
#include "SDLIncludes.h"
#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <thread>
#include <string>
#include <utility>
class CRelayUdpClient {
public:
 // sourceUs is the origin's own monotonic stamp (ordering only). When timed, ageUs
 // is the measured transit age at receivedUs and uncertaintyUs bounds its error.
 // mask: the origin's latest streamed-word mask (RelayUdp::MaskSize bytes, bit w =
 // segment word w travels by UDP alone), empty until one has been received.
 // ageUs = transitUs + sampleAgeUs: how old the data is at receivedUs.
 struct Sample { std::vector<uint8_t> raw,mask;uint64_t sourceUs=0,receivedUs=0,ageUs=0,transitUs=0,uncertaintyUs=0,controlUs=0;uint32_t sampleAgeUs=0,sampleUncertaintyUs=0;uint16_t heard=0;bool timed=false; };
 static constexpr uint64_t MaxAgeUs=2000000;
 static uint64_t Now() { return uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()); }
 CRelayUdpClient(const std::string& host,uint16_t port,const uint8_t* token,unsigned index,unsigned count,size_t size,const RelayUdp::HostConfig* hosted=nullptr)
  :m_index(index),m_count(count),m_size(size),m_samples(count),m_assemblies(count),m_alive(count),m_heard(count),m_heardMeUs(count),m_sequences(count),m_peerBases(count),m_held(count),m_peerMasks(count),m_delayMeters(count),m_lossMeters(count) {
  std::memcpy(m_token.data(),token,16);
  if(SDLNet_ResolveHost(&m_server,host.c_str(),port)<0) return;
  if(hosted) { m_hosted=true;m_hostConfig=*hosted;m_hostAddress.host=hosted->ip;m_hostAddress.port=SDL_SwapBE16(hosted->port);if(index==0) SDLNet_ResolveHost(&m_hostAddress,"127.0.0.1",hosted->port); }
  m_socket=SDLNet_UDP_Open(0);
  if(!m_socket) return;
  m_submitTx=SDLNet_AllocPacket(int(RelayUdp::DatagramSize));
  if(!m_submitTx) { SDLNet_UDP_Close(m_socket);m_socket=nullptr;return; }
  m_thread=std::thread([this]{ Run(); });
 }
 ~CRelayUdpClient() {
  m_stop=true;if(m_thread.joinable()) m_thread.join();
  if(m_submitTx) SDLNet_FreePacket(m_submitTx);
  if(m_socket) SDLNet_UDP_Close(m_socket);
 }
 bool SelfHostedActive() const { return m_usingHost.load(); }
 // Adaptive hosts (RLT2) read the receive-side queueing delay / loss we put in
 // every hello: Header::total = p50 queueing delay in ms over the last second,
 // Header::part = loss percent. Older hosts reject hellos with those fields set,
 // so they stay zero until the host has announced itself.
 void EnableFeedback(bool on) { m_feedback=on; }
 void SetTickRate(unsigned hz) { m_tickRate=std::clamp(hz,5u,60u); }
 struct Feedback { unsigned queueMs=0,lossPercent=0; };
 Feedback LastFeedback() { std::lock_guard<std::mutex> l(m_mutex);return m_lastFeedback; }
 bool Available() const { return m_socket!=nullptr; }
 unsigned Index() const { return m_index; }
 unsigned Count() const { return m_count; }
 bool Active() const { const auto last=m_lastAck.load();const auto now=Now();return last && now>=last && now-last<2000000; }
 // freshUs: how long a peer may stay quiet before it stops counting as heard.
 // mask (optional, MaskSize(size) bytes): this cabinet's streamed-word mask. It
 // is appended to the next snapshot when it changed (and repeated twice against
 // loss) and at least every MaskRefreshUs, so receivers can apply streamed words
 // while a TCP control message is still in flight.
 static constexpr uint64_t MaskRefreshUs=500000;
 // sampleAgeUs/sampleUncertaintyUs: how long before sourceUs the game wrote the
 // segment, and how precisely that is known (Trailer::sampleAgeUs).
 void Submit(const void* bytes,size_t size,uint64_t sourceUs,uint64_t freshUs,uint64_t controlUs=0,const std::vector<uint8_t>* mask=nullptr,
             uint64_t sampleAgeUs=0,uint64_t sampleUncertaintyUs=0) {
  if(size!=m_size || !sourceUs || !Active()) return;
  const auto* p=static_cast<const uint8_t*>(bytes);
  std::vector<uint8_t> pending(p,p+size);
  const auto now=Now();
  RelayUdp::Trailer t;t.controlUs=controlUs;
  t.sampleAgeUs=uint32_t(std::min<uint64_t>(sampleAgeUs,1000000));t.sampleUncertaintyUs=uint32_t(std::min<uint64_t>(sampleUncertaintyUs,1000000));
  const bool useHost=m_usingHost.load();
  {
   std::lock_guard<std::mutex> l(m_mutex);
   m_freshUs=freshUs;
   if(mask && mask->size()==RelayUdp::MaskSize(m_size)) {
    if(*mask!=m_mask) { m_mask=*mask;m_maskRepeat=3; }
   } else { m_mask.clear();m_maskRepeat=0; }
   for(unsigned i=0;i<m_count;++i) if(i!=m_index && Fresh(m_alive[i],now)) t.heard|=uint16_t(1u<<i);
   if(m_localBase.sourceUs && m_localBase.bytes.size()==m_size) {
    t.baseUs=m_localBase.sourceUs;
    for(size_t i=0;i<m_size;++i) pending[i]^=m_localBase.bytes[i];
    ++m_stats.sentDelta;
   }
   ++m_stats.sent;m_stats.sentRawBytes+=m_size+sizeof(t);
   if(!m_mask.empty() && (m_maskRepeat || !m_maskSentUs || now<m_maskSentUs || now-m_maskSentUs>=MaskRefreshUs)) {
    t.flags|=RelayUdp::FlagMask;if(m_maskRepeat) --m_maskRepeat;m_maskSentUs=now;++m_stats.maskSent;
   }
   int64_t offset=0;uint64_t rtt=0;
   if(Estimate(m_clocks[useHost?1:0],now,offset,rtt) && int64_t(sourceUs)+offset>0) {
    t.clock=useHost?2:1;t.rttUs=uint32_t(std::min<uint64_t>(rtt,UINT32_MAX));t.clockUs=uint64_t(int64_t(sourceUs)+offset);
   }
   pending.resize(pending.size()+sizeof(t));std::memcpy(pending.data()+m_size,&t,sizeof(t));
   if(t.flags&RelayUdp::FlagMask) pending.insert(pending.end(),m_mask.begin(),m_mask.end());
  }
  pending=RelayUdp::Encode(pending.data(),pending.size());
  RelayUdp::Header h;h.kind=RelayUdp::Snapshot;h.origin=uint16_t(m_index);h.total=uint16_t(pending.size());
  h.parts=uint8_t((pending.size()+RelayUdp::ChunkSize-1)/RelayUdp::ChunkSize);h.sourceUs=sourceUs;
  std::memcpy(h.token,m_token.data(),16);
  std::lock_guard<std::mutex> s(m_sendMutex);
  h.sequence=++m_seq;
  m_stats.sentBytes+=pending.size(); // m_sendMutex guards this counter
  for(h.part=0;h.part<h.parts;++h.part)
   Transmit(m_submitTx,h,pending.data()+h.part*RelayUdp::ChunkSize,std::min(RelayUdp::ChunkSize,pending.size()-h.part*RelayUdp::ChunkSize),useHost?m_hostAddress:m_server);
 }
 // Delta bases. The TCP ring is the reference both ends share. A segment this
 // cabinet originates by TCP becomes a candidate; it becomes THE base once its
 // self-echo has returned, because every cabinet has then registered it (so
 // deltas never wait for a keyframe that is still in transit, which matters
 // when the ring also runs at the tick rate, e.g. Sega Rally 2). Receivers keep
 // the last few keyframes per peer so a base still in use is never evicted.
 static constexpr size_t MaxLocalCandidates=64, PeerBaseHistory=16;
 void NoteLocalKeyframe(uint64_t sourceUs,const void* bytes,size_t size) {
  if(size!=m_size || !sourceUs) return;
  const auto* p=static_cast<const uint8_t*>(bytes);
  std::lock_guard<std::mutex> l(m_mutex);
  if(!m_localCandidates.empty() && sourceUs<=m_localCandidates.back().sourceUs) return;
  m_localCandidates.push_back({std::vector<uint8_t>(p,p+size),sourceUs});
  while(m_localCandidates.size()>MaxLocalCandidates) m_localCandidates.pop_front();
 }
 // The ring returned our segment with this stamp: all peers hold it now.
 void CommitLocalKeyframe(uint64_t sourceUs) {
  std::lock_guard<std::mutex> l(m_mutex);
  while(!m_localCandidates.empty() && m_localCandidates.front().sourceUs<sourceUs) m_localCandidates.pop_front();
  if(m_localCandidates.empty() || m_localCandidates.front().sourceUs!=sourceUs) return;
  m_localBase=std::move(m_localCandidates.front());m_localCandidates.pop_front();
 }
 void SetPeerBase(unsigned origin,uint64_t sourceUs,const void* bytes,size_t size) {
  if(origin>=m_count || origin==m_index || size!=m_size || !sourceUs) return;
  const auto* p=static_cast<const uint8_t*>(bytes);
  std::lock_guard<std::mutex> l(m_mutex);
  auto& history=m_peerBases[origin];
  if(!history.empty() && sourceUs<=history.back().sourceUs) return; // the ring never moves a slot backwards
  history.push_back({std::vector<uint8_t>(p,p+size),sourceUs});
  while(history.size()>PeerBaseHistory) history.pop_front();
  // A delta that arrived before its keyframe can be undone now.
  auto& h=m_held[origin];
  if(!h.raw.empty()) {
   if(h.baseUs==sourceUs) { Resolve(h,history.back());Store(origin,h);++m_stats.heldResolved; }
   else if(h.baseUs<sourceUs) ++m_stats.deltaStale; // superseded by a newer keyframe
   h=HeldSample{};
  }
 }
 void ClearBases() {
  std::lock_guard<std::mutex> l(m_mutex);
  m_localBase=PeerBase{};m_localCandidates.clear();
  for(auto& b:m_peerBases) b.clear();
  for(auto& h:m_held) h=HeldSample{};
  for(auto& m:m_peerMasks) m=PeerMask{};
  m_mask.clear();m_maskRepeat=0;m_maskSentUs=0;
 }
 // datagrams/lost/reordered/duplicated come from the per-peer sequence
 // numbers: lost = sequences never seen (a late arrival takes itself back out),
 // reordered = arrived after a newer one, duplicated = seen twice.
 struct Stats { uint64_t sent=0,sentDelta=0,sentBytes=0,sentRawBytes=0,received=0,receivedDelta=0,heldResolved=0,deltaStale=0,deltaBaseUnknown=0,maskSent=0,maskReceived=0,
                datagrams=0,lost=0,reordered=0,duplicated=0; };
 Stats GetStats() { std::lock_guard<std::mutex> l(m_mutex);std::lock_guard<std::mutex> s(m_sendMutex);return m_stats; }
 // Returns only a sample strictly newer than afterSourceUs (no copy otherwise).
 bool Latest(unsigned origin,Sample& sample,uint64_t afterSourceUs=0) {
  std::lock_guard<std::mutex> l(m_mutex);
  if(origin>=m_count || origin==m_index || m_samples[origin].raw.empty() || m_samples[origin].sourceUs<=afterSourceUs) return false;
  sample=m_samples[origin];sample.mask=m_peerMasks[origin].bytes;return true;
 }
 // True while every other cabinet is sending usable timed snapshots AND reports
 // that it is receiving ours. Only then may the reliable ring stay silent.
 // holding: the lane is already primary. Then a peer may stay quiet (or fail
 // to report us) for LeaveUs before the ring resumes, so a few lost or late
 // snapshots do not cost a burst of TCP keyframes; becoming primary still
 // needs everyone within freshUs.
 static constexpr uint64_t MinLeaveUs=400000;
 uint64_t LeaveUs() const { return std::max<uint64_t>(MinLeaveUs,2*m_freshUs); }
 bool PrimaryReady(bool holding=false) {
  if(!Active()) return false;
  const auto now=Now();
  std::lock_guard<std::mutex> l(m_mutex);
  const uint64_t window=holding ? LeaveUs() : m_freshUs;
  for(unsigned i=0;i<m_count;++i) {
   if(i==m_index) continue;
   if(!Fresh(m_alive[i],now,window)) return false;
   if(holding ? !(m_heardMeUs[i] && now>=m_heardMeUs[i] && now-m_heardMeUs[i]<=window) : !(m_heard[i]&(1u<<m_index))) return false;
  }
  return true;
 }
private:
 // Caller holds m_sendMutex: SDL_net's send bookkeeping is not thread-safe.
 void Transmit(UDPpacket* tx,const RelayUdp::Header& h,const uint8_t* p,size_t n,const IPaddress& address) {
  std::memcpy(tx->data,&h,sizeof(h));if(n) std::memcpy(tx->data+sizeof(h),p,n);
  tx->len=int(sizeof(h)+n);tx->address=address;SDLNet_UDP_Send(m_socket,-1,tx);
 }
 struct ClockSample { uint64_t rttUs=0,at=0;int64_t offset=0; };
 struct Clock { std::array<ClockSample,8> samples{};unsigned next=0; };
 // A peer is heard while timed, decodable snapshots keep arriving. A delta
 // still waiting for its keyframe does not count: if it never resolved, the
 // ring must not fall silent on its account.
 struct Alive { uint64_t receivedUs=0;bool timed=false; };
 bool Fresh(const Alive& a,uint64_t now) const { return Fresh(a,now,m_freshUs); }
 static bool Fresh(const Alive& a,uint64_t now,uint64_t window) { return a.timed && a.receivedUs && now>=a.receivedUs && now-a.receivedUs<=window; }
 struct PeerBase { std::vector<uint8_t> bytes;uint64_t sourceUs=0; };
 // Sequence bookkeeping per peer: a 64-wide bitmap of recently seen sequences.
 struct Sequences {
  bool any=false;uint32_t last=0;uint64_t seen=0;
  void Note(const RelayUdp::Header& h,Stats& s) {
   if(h.part) return; // one count per snapshot: parts share the sequence
   ++s.datagrams;
   if(!any) { any=true;last=h.sequence;seen=1;return; }
   if(RelayUdp::Newer(h.sequence,last)) {
    const uint32_t gap=h.sequence-last;
    s.lost+=gap-1;seen=gap>=64?0:seen<<gap;seen|=1;last=h.sequence;return;
   }
   const uint32_t back=last-h.sequence;
   if(back<64 && !(seen&(uint64_t(1)<<back))) { seen|=uint64_t(1)<<back;++s.reordered;if(s.lost) --s.lost;return; }
   ++s.duplicated;
  }
 };
 struct HeldSample : Sample { uint64_t baseUs=0; };
 static void Resolve(Sample& s,const PeerBase& b) { for(size_t i=0;i<s.raw.size() && i<b.bytes.size();++i) s.raw[i]^=b.bytes[i]; }
 // Publish a resolved sample unless a newer one is already there.
 void Store(unsigned origin,const Sample& s) {
  auto& a=m_alive[origin];if(s.receivedUs>=a.receivedUs) a={s.receivedUs,s.timed};
  auto& d=m_samples[origin];if(s.sourceUs>d.sourceUs) d=s;
 }
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
   std::lock_guard<std::mutex> s(m_sendMutex);Transmit(tx,h,p,n,address);
  };
  // Block for incoming datagrams (ours and, when self-hosting, the host socket's)
  // instead of polling; the timeout only paces hellos and route checks.
  SDLNet_SocketSet set=SDLNet_AllocSocketSet(2);
  if(set) { SDLNet_UDP_AddSocket(set,m_socket);if(host && host->Socket()) SDLNet_UDP_AddSocket(set,host->Socket()); }
  uint64_t nextHello=0,hostAck=0,routeAt=0;bool hostRoute=false,lastRoute=false,haveRoute=false;uint32_t helloSeq=0,routeSeq=0;
  std::array<std::pair<uint32_t,uint64_t>,8> helloWindow{};
  while(!m_stop.load()) {
   const auto now=Now();
   if(host) { std::lock_guard<std::mutex> s(m_sendMutex);host->Poll(rx,tx,now); }
   if(now>=nextHello) {
    RelayUdp::Header h;std::memcpy(h.token,m_token.data(),16);h.origin=uint16_t(m_index);
    h.sequence=++helloSeq;h.sourceUs=now;helloWindow[helloSeq%helloWindow.size()]={helloSeq,now};
    if(m_feedback.load()) { const auto f=CloseFeedbackWindow(now);h.total=uint16_t(std::min(f.queueMs,65535u));h.part=uint8_t(std::min(f.lossPercent,100u)); }
    send(h,nullptr,0,m_server);
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
    { std::lock_guard<std::mutex> l(m_mutex);m_sequences[h.origin].Note(h,m_stats);m_lossMeters[h.origin].Note(h.sequence); }
    std::vector<uint8_t> encoded,raw;
    const auto received=Now();
    const size_t maskSize=RelayUdp::MaskSize(m_size),plain=m_size+RelayUdp::TrailerSize;
    if(!m_assemblies[h.origin].Add(h,rx->data+sizeof(h),received,encoded) || !RelayUdp::Decode(encoded,plain,plain+maskSize,raw)) continue;
    RelayUdp::Trailer t;std::memcpy(&t,raw.data()+m_size,sizeof(t));
    const bool hasMask=(t.flags&RelayUdp::FlagMask)!=0;
    if(raw.size()!=(hasMask?plain+maskSize:plain)) continue;
    std::lock_guard<std::mutex> l(m_mutex);
    if(hasMask && h.sourceUs>m_peerMasks[h.origin].sourceUs) {
     m_peerMasks[h.origin].bytes.assign(raw.begin()+plain,raw.end());m_peerMasks[h.origin].sourceUs=h.sourceUs;++m_stats.maskReceived;
    }
    raw.resize(m_size);
    if(h.sourceUs<=m_samples[h.origin].sourceUs) continue;
    HeldSample s;
    s.raw=std::move(raw);s.sourceUs=h.sourceUs;s.receivedUs=received;s.heard=t.heard;s.controlUs=t.controlUs;s.baseUs=t.baseUs;
    s.sampleAgeUs=t.sampleAgeUs;s.sampleUncertaintyUs=t.sampleUncertaintyUs;
    // Transit = (receive time on the shared clock) - (send time on the shared
    // clock); the data is older still by the sender's sampleAgeUs.
    int64_t offset=0;uint64_t rtt=0;
    if((t.clock==1 || t.clock==2) && t.clockUs && t.clockUs<=uint64_t(INT64_MAX) && Estimate(m_clocks[t.clock-1],received,offset,rtt)) {
     const int64_t transit=int64_t(received)+offset-int64_t(t.clockUs);
     s.uncertaintyUs=rtt/2+uint64_t(t.rttUs)/2;
     // Tolerate a negative transit within the measurement error; reject implausible clocks.
     if(transit>=-int64_t(s.uncertaintyUs)-2000 && transit<=int64_t(MaxAgeUs)) {
      s.transitUs=uint64_t(std::max<int64_t>(0,transit));s.ageUs=s.transitUs+t.sampleAgeUs;s.uncertaintyUs+=t.sampleUncertaintyUs;s.timed=true;
      m_delayMeters[h.origin].Note(transit); // m_mutex held
     }
    }
    ++m_stats.received;
    m_heard[h.origin]=t.heard;
    if(t.heard&(1u<<m_index)) m_heardMeUs[h.origin]=received;
    if(!t.baseUs) { Store(h.origin,s);continue; }
    ++m_stats.receivedDelta;
    const auto& history=m_peerBases[h.origin];
    const auto base=std::find_if(history.begin(),history.end(),[&](const PeerBase& b){ return b.sourceUs==t.baseUs; });
    if(base!=history.end()) { Resolve(s,*base);Store(h.origin,s); }
    else if(history.empty() || t.baseUs>history.back().sourceUs) {
     // Keyframe still in transit on the TCP ring: keep the newest delta until it lands.
     ++m_stats.deltaBaseUnknown;
     auto& held=m_held[h.origin];
     if(held.raw.empty() || s.sourceUs>held.sourceUs) held=std::move(s);
    }
    else ++m_stats.deltaStale; // built on a keyframe older than our history: newer TCP segments already superseded it
   }
   const auto routeNow=Now();
   const bool useHost=m_hosted && hostRoute && routeAt && routeNow>=routeAt && routeNow-routeAt<1000000 && hostAck && routeNow>=hostAck && routeNow-hostAck<1500000;
   m_usingHost=useHost;
   if(m_hosted && useHost!=lastRoute) {std::fprintf(stderr,"UDP self-host: snapshots via %s.\n",useHost?"creator":"central relay fallback");lastRoute=useHost;}
   if(set) SDLNet_CheckSockets(set,10); else std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  if(set) SDLNet_FreeSocketSet(set);
  SDLNet_FreePacket(tx);SDLNet_FreePacket(rx);
 }
 const unsigned m_index,m_count;const size_t m_size;
 bool m_hosted=false;RelayUdp::HostConfig m_hostConfig;IPaddress m_hostAddress{};
 UDPsocket m_socket=nullptr;IPaddress m_server{};std::array<uint8_t,16> m_token{};
 std::atomic<bool> m_stop{false},m_usingHost{false};std::atomic<uint64_t> m_lastAck{0};std::thread m_thread;
 std::mutex m_mutex;std::vector<Sample> m_samples;std::vector<RelayUdp::Assembly> m_assemblies;
 std::vector<Alive> m_alive;std::vector<uint16_t> m_heard;std::vector<uint64_t> m_heardMeUs;std::vector<Sequences> m_sequences;
 std::vector<std::deque<PeerBase>> m_peerBases;std::vector<HeldSample> m_held;
 std::deque<PeerBase> m_localCandidates;PeerBase m_localBase;Stats m_stats;
 // Receive-side congestion feedback for adaptive hosts (see EnableFeedback).
 std::atomic<bool> m_feedback{false};std::atomic<unsigned> m_tickRate{60};
 Feedback m_lastFeedback;uint64_t m_feedbackClosedUs=0;
 Feedback CloseFeedbackWindow(uint64_t now) {
  // Hellos go out twice a second; close the meters once a second so the loss
  // percentage has a sensible sample count.
  std::lock_guard<std::mutex> l(m_mutex);
  if(m_feedbackClosedUs && now-m_feedbackClosedUs<900000) return m_lastFeedback;
  m_feedbackClosedUs=now;Feedback out;
  for(unsigned i=0;i<m_count;++i) {
   if(i==m_index) continue;
   out.queueMs=std::max(out.queueMs,unsigned(std::max<int64_t>(0,m_delayMeters[i].Close())/1000));
   out.lossPercent=std::max(out.lossPercent,m_lossMeters[i].ClosePercent());
  }
  m_lastFeedback=out;return out;
 }
 struct PeerMask { std::vector<uint8_t> bytes;uint64_t sourceUs=0; };
 std::vector<PeerMask> m_peerMasks;std::vector<Online::DelayMeter> m_delayMeters;std::vector<Online::LossMeter> m_lossMeters; // meters under m_mutex
 std::vector<uint8_t> m_mask;unsigned m_maskRepeat=0;uint64_t m_maskSentUs=0;
 uint64_t m_freshUs=300000;
 std::mutex m_sendMutex;UDPpacket* m_submitTx=nullptr;uint32_t m_seq=0;
 std::array<Clock,2> m_clocks{}; // 0 = central relay, 1 = creator host
};
#endif
