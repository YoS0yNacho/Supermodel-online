// Relay UDP snapshot lane. In-race snapshots travel here (latest wins, never
// resent); setup, race-start control and state-change keyframes remain TCP.
// Version 4: a snapshot may be a delta (XOR) against a TCP keyframe both
// sides already hold; Trailer::baseUs names it. The relay never decodes.
// Version 5: the trailer also says how old the segment data already was when
// the snapshot was taken (sampleAgeUs) and how precisely that is known.
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
// Every snapshot carries a 40-byte trailer after the native segment (see
// Trailer), optionally followed by the sender's streamed-word mask: one bit per
// aligned 32-bit word of the segment, set when that word travels by UDP alone.
constexpr size_t MaxSegmentSize=3264, TrailerSize=40, MaxMaskSize=(MaxSegmentSize/4+7)/8;
constexpr size_t MaxRawSize=MaxSegmentSize+TrailerSize+MaxMaskSize, MaxEncodedSize=MaxRawSize+1;
inline size_t MaskSize(size_t segmentSize) { return (segmentSize/4+7)/8; }
constexpr uint8_t FlagMask=1; // Trailer::flags: the streamed-word mask follows the trailer
constexpr size_t AckClockSize=8; // Ack = header + responder clock (microseconds, little endian)
constexpr uint8_t Version=5, Hello=1, Ack=2, Snapshot=3, HostStatus=4, Route=5;
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
 uint8_t clock=0,flags=0;
 uint32_t rttUs=0;
 uint64_t clockUs=0;
 // Stamp of the sender's last TCP control message. The receiver holds this
 // snapshot until that message has arrived, so control stays reliable and ordered.
 uint64_t controlUs=0;
 // 0: the segment is sent as is. Otherwise the segment bytes are XORed with the
 // sender's TCP segment of that stamp (its keyframe); the receiver XORs them
 // back with its copy of the same keyframe. Between control messages only the
 // streamed words differ, so the zero-run encoding below removes nearly all of
 // the payload.
 uint64_t baseUs=0;
 // The game wrote the segment sampleAgeUs before clockUs/sourceUs (the write
 // happened in the previous emulated frame), known to within
 // sampleUncertaintyUs. Receivers add both to the transit age and its error
 // instead of charging a whole frame of uncertainty to every snapshot.
 uint32_t sampleAgeUs=0,sampleUncertaintyUs=0;
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
// The decoded size must lie in [minExpected,maxExpected]; the caller checks the
// exact size against the trailer flags.
inline bool Decode(const std::vector<uint8_t>& in,size_t minExpected,size_t maxExpected,std::vector<uint8_t>& out) {
 if(in.empty() || in.size()>MaxEncodedSize || !minExpected || minExpected>maxExpected || maxExpected>MaxRawSize) return false;
 std::vector<uint8_t> tmp;tmp.reserve(maxExpected);
 if(in[0]==0) { if(in.size()<minExpected+1 || in.size()>maxExpected+1) return false;tmp.assign(in.begin()+1,in.end()); }
 else if(in[0]==1) {
  for(size_t i=1;i<in.size();) {
   if(in[i]) { if(tmp.size()==maxExpected) return false;tmp.push_back(in[i++]); }
   else { if(i+1>=in.size() || !in[i+1] || tmp.size()+in[i+1]>maxExpected) return false;tmp.insert(tmp.end(),in[i+1],0);i+=2; }
  }
  if(tmp.size()<minExpected) return false;
 } else return false;
 out.swap(tmp);return true;
}
inline bool Decode(const std::vector<uint8_t>& in,size_t expected,std::vector<uint8_t>& out) { return Decode(in,expected,expected,out); }
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
