// SGT UDP snapshots are read-only presentation inputs; never native RX writes.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_SGT24H_POSE_H
#define INCLUDED_SGT24H_POSE_H
#include "Position.h"
#include "Network/Online/Sgt24h.h"
#include <array>
#include <algorithm>
namespace Sgt24hPose {
constexpr uint64_t FreshUs=100000,UncertaintyUs=50000;
using Wire=std::array<uint8_t,Sgt24hOnline::PoseWireBytes>;
inline uint64_t Get(const uint8_t* p,unsigned n){uint64_t v=0;for(unsigned i=0;i<n;++i)v|=uint64_t(p[i])<<(8*i);return v;}
inline void Put(uint8_t* p,uint64_t v,unsigned n){for(unsigned i=0;i<n;++i)p[i]=uint8_t(v>>(8*i));}
inline uint64_t Hash(const uint8_t* row,size_t n){if(!row || n!=Sgt24h::RowBytes)return 0;uint64_t h=14695981039346656037ull;for(size_t i=0;i<n;++i)h=(h^row[i])*1099511628211ull;return h?h:1;}
struct Packet {
 uint64_t epoch=0;uint32_t seq=0;unsigned origin=0,count=0,n=0;
 std::array<uint64_t,4> keys{};std::array<uint8_t,Sgt24h::RowBytes> row{};
};
static_assert(Wire{}.size()==64+Sgt24h::RowBytes);
inline Wire Encode(const Packet& p) {
 Wire w{};std::memcpy(w.data(),"SGP1",4);w[4]=uint8_t(p.origin);w[5]=uint8_t(p.count);w[6]=uint8_t(p.n);
 Put(w.data()+8,p.epoch,8);Put(w.data()+16,p.seq,4);
 for(unsigned i=0;i<4;++i)Put(w.data()+32+8*i,p.keys[i],8);
 std::copy(p.row.begin(),p.row.end(),w.begin()+64);return w;
}
inline bool Decode(const uint8_t* w,size_t size,unsigned origin,unsigned count,Packet& out) {
 if(!w || size!=Wire{}.size() || std::memcmp(w,"SGP1",4) || count<2 || count>4 || origin>=count ||
    w[4]!=origin || w[5]!=count || !w[6] || w[6]>4 || w[7] || !Get(w+8,8))return false;
 for(unsigned i=20;i<32;++i)if(w[i])return false;
 Packet p;p.origin=origin;p.count=count;p.n=w[6];p.epoch=Get(w+8,8);p.seq=uint32_t(Get(w+16,4));
 std::copy(w+64,w+size,p.row.begin());
 if(p.row[2]!=3 || p.row[3]>=count)return false;
 for(unsigned i=0;i<4;++i){p.keys[i]=Get(w+32+8*i,8);if((i<p.n)!=bool(p.keys[i]))return false;}
 if(p.keys[0]!=Hash(p.row.data(),p.row.size()))return false;
 out=p;return true;
}
inline bool HasNativeKey(const Packet& p,uint64_t key){return key && p.n<=4 && std::find(p.keys.begin(),p.keys.begin()+p.n,key)!=p.keys.begin()+p.n;}
struct Received {Packet packet{};uint64_t stamp=0,received=0,age=0,uncertainty=0;};
class Sender {
 Packet packet{};
public:
 void Clear(){packet={};}
 const Packet* Push(const uint8_t* row,size_t size,unsigned origin,unsigned count,uint64_t epoch) {
  if(!row || size!=Sgt24h::RowBytes || count<2 || count>4 || origin>=count || !epoch || row[2]!=3 || row[3]>=count){Clear();return nullptr;}
  if(packet.epoch!=epoch || packet.origin!=origin || packet.count!=count || packet.row[3]!=row[3])Clear();
  const auto key=Hash(row,size);if(packet.n && key==packet.keys[0])return nullptr;
  if(packet.seq==UINT32_MAX){Clear();return nullptr;}
  for(unsigned i=3;i>0;--i)packet.keys[i]=packet.keys[i-1];
  packet.keys[0]=key;packet.n=std::min(4u,packet.n+1);++packet.seq;
  packet.epoch=epoch;packet.origin=origin;packet.count=count;std::copy(row,row+size,packet.row.begin());return &packet;
 }
};
}
#endif
