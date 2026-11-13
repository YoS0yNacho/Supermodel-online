// Relay UDP snapshot lane. In-race snapshots travel here (latest wins, never
// resent); setup, race-start control and state-change keyframes remain TCP.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_RELAY_UDP_WIRE_H
#define INCLUDED_RELAY_UDP_WIRE_H
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
namespace RelayUdp {
constexpr size_t DatagramSize=1200, HeaderSize=40, ChunkSize=DatagramSize-HeaderSize;
// Every snapshot carries a 24-byte trailer after the native segment (see Trailer).
constexpr size_t MaxSegmentSize=3264, TrailerSize=24, MaxRawSize=MaxSegmentSize+TrailerSize, MaxEncodedSize=MaxRawSize+1;
constexpr size_t AckClockSize=8; // Ack = header + responder clock (microseconds, little endian)
constexpr uint8_t Version=3, Hello=1, Ack=2, Snapshot=3, HostStatus=4, Route=5;
#pragma pack(push,1)
struct Header {
 uint32_t magic=0x31504755; // UGP1, little endian
 uint8_t version=Version,kind=Hello,part=0,parts=0;
 uint8_t token[16]={};
 uint32_t sequence=0;
 uint16_t origin=0,total=0;
 uint64_t sourceUs=0;
};
// heard: bit i set = the sender currently receives usable snapshots from cabinet i.
// clock: 0 = none, 1 = central relay clock, 2 = creator-host clock; clockUs is the
// sample time on that clock and rttUs the sender's round trip to that responder.
struct Trailer {
 uint16_t heard=0;
 uint8_t clock=0,reserved=0;
 uint32_t rttUs=0;
 uint64_t clockUs=0;
 // Stamp of the sender's last TCP control message. The receiver holds this
 // snapshot until that message has arrived, so control stays reliable and ordered.
 uint64_t controlUs=0;
};
#pragma pack(pop)
static_assert(sizeof(Header)==HeaderSize,"UDP header wire size");
static_assert(sizeof(Trailer)==TrailerSize,"UDP trailer wire size");
inline bool Newer(uint32_t a,uint32_t b) { return a!=b && uint32_t(a-b)<0x80000000u; }
inline bool Parse(const void* bytes,size_t n,Header& h) {
 if(n<HeaderSize || n>DatagramSize) return false;
 std::memcpy(&h,bytes,sizeof(h));
 if(h.magic!=Header{}.magic || h.version!=Version) return false;
 if(h.kind==Ack) return n==HeaderSize+AckClockSize && !h.part && !h.parts && !h.total;
 if(h.kind==Hello || h.kind==HostStatus || h.kind==Route) return n==HeaderSize && !h.part && !h.parts && !h.total;
 if(h.kind!=Snapshot || h.total<2 || h.total>MaxEncodedSize || !h.sourceUs || h.sourceUs>uint64_t(INT64_MAX)) return false;
 const size_t parts=(h.total+ChunkSize-1)/ChunkSize;
 return h.parts==parts && h.part<parts && n==HeaderSize+std::min(ChunkSize,size_t(h.total)-h.part*ChunkSize);
}
// Bounded zero-run encoding: no dependencies, no decompression bombs. Use raw
// mode when encoding would grow the payload. The relay never decodes game bytes.
inline std::vector<uint8_t> Encode(const void* data,size_t n) {
 if(!data || !n || n>MaxRawSize) return {};
 const auto* p=static_cast<const uint8_t*>(data);
 std::vector<uint8_t> out{1};out.reserve(n+1);
 for(size_t i=0;i<n;) {
  if(p[i]) out.push_back(p[i++]);
  else { size_t j=i;while(j<n && !p[j] && j-i<255) ++j;out.push_back(0);out.push_back(uint8_t(j-i));i=j; }
 }
 if(out.size()>n) { out.resize(n+1);out[0]=0;std::memcpy(out.data()+1,p,n); }
 return out;
}
inline bool Decode(const std::vector<uint8_t>& in,size_t expected,std::vector<uint8_t>& out) {
 if(in.empty() || in.size()>MaxEncodedSize || !expected || expected>MaxRawSize) return false;
 std::vector<uint8_t> tmp;tmp.reserve(expected);
 if(in[0]==0) { if(in.size()!=expected+1) return false;tmp.assign(in.begin()+1,in.end()); }
 else if(in[0]==1) {
  for(size_t i=1;i<in.size();) {
   if(in[i]) { if(tmp.size()==expected) return false;tmp.push_back(in[i++]); }
   else { if(i+1>=in.size() || !in[i+1] || tmp.size()+in[i+1]>expected) return false;tmp.insert(tmp.end(),in[i+1],0);i+=2; }
  }
  if(tmp.size()!=expected) return false;
 } else return false;
 out.swap(tmp);return true;
}
struct Assembly {
 Header header{};bool active=false,delivered=false;
 uint32_t last=0;uint64_t beganUs=0;
 uint8_t mask=0;std::vector<uint8_t> bytes;
 bool Add(const Header& h,const uint8_t* payload,uint64_t now,std::vector<uint8_t>& out) {
  if(delivered && !Newer(h.sequence,last)) return false;
  if(active && h.sequence!=header.sequence && !Newer(h.sequence,header.sequence)) return false;
  if(!active || h.sequence!=header.sequence || now<beganUs || now-beganUs>250000) {
   header=h;active=true;beganUs=now;mask=0;bytes.assign(h.total,0);
  }
  if(h.total!=header.total || h.parts!=header.parts || h.sourceUs!=header.sourceUs) return false;
  const auto bit=uint8_t(1u<<h.part);
  if(mask&bit) return false;
  const size_t len=std::min(ChunkSize,size_t(h.total)-h.part*ChunkSize);
  std::memcpy(bytes.data()+h.part*ChunkSize,payload,len);mask|=bit;
  if(mask!=uint8_t((1u<<h.parts)-1)) return false;
  out=bytes;last=h.sequence;delivered=true;active=false;return true;
 }
};
}
#endif
