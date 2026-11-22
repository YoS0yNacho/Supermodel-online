#include "Network/Games/Sgt24h/Pose.h"
#include <cassert>
using namespace Sgt24hPose;
int main() {
 std::array<uint8_t,Sgt24h::RowBytes> row{};row[2]=3;row[3]=0;
 Sender sender;Packet p;const Packet* packet=sender.Push(row.data(),row.size(),0,4,7);assert(packet);
 auto w=Encode(*packet);assert(Decode(w.data(),w.size(),0,4,p));
 assert(p.epoch==7 && p.n==1 && p.seq==1 && HasNativeKey(p,Hash(row.data(),row.size())));
 assert(!sender.Push(row.data(),row.size(),0,4,7));
 const auto first=p.keys[0];
 for(unsigned i=1;i<4;++i){row[0x2c]=uint8_t(i);packet=sender.Push(row.data(),row.size(),0,4,7);assert(packet);}
 assert(packet->n==4 && HasNativeKey(*packet,first));
 row[0x2c]=4;packet=sender.Push(row.data(),row.size(),0,4,7);assert(packet && !HasNativeKey(*packet,first));
 row[3]=1;packet=sender.Push(row.data(),row.size(),0,4,7);assert(packet && packet->n==1);
 packet=sender.Push(row.data(),row.size(),0,4,8);assert(packet && packet->epoch==8 && packet->n==1);
 w=Encode(*packet);assert(Decode(w.data(),w.size(),0,4,p));
 const Packet before=p;
 auto invalid=[&](Wire bad){assert(!Decode(bad.data(),bad.size(),0,4,p));assert(p.epoch==before.epoch && p.row==before.row && p.keys==before.keys);};
 for(unsigned offset:{0u,4u,5u,7u,20u,31u,64u+0x2cu}) {auto bad=w;bad[offset]^=0x80;invalid(bad);}
 for(unsigned n:{0u,5u,255u}){auto bad=w;bad[6]=uint8_t(n);invalid(bad);}
 auto bad=w;for(unsigned i=8;i<16;++i)bad[i]=0;invalid(bad);
 bad=w;bad[40]=1;invalid(bad); // unused history entry must be zero
 for(size_t n=0;n<w.size();++n)assert(!Decode(w.data(),n,0,4,p));
 assert(!Decode(nullptr,w.size(),0,4,p));assert(!Decode(w.data(),w.size()+1,0,4,p));
 assert(!Decode(w.data(),w.size(),1,4,p));assert(!Decode(w.data(),w.size(),0,3,p));
 row[2]=4;assert(!sender.Push(row.data(),row.size(),0,4,8));row[2]=3;
 packet=sender.Push(row.data(),row.size(),0,4,8);assert(packet && packet->n==1);
 for(unsigned count:{0u,1u,5u,~0u})assert(!sender.Push(row.data(),row.size(),0,count,8));
 assert(!sender.Push(row.data(),row.size(),4,4,8));
 assert(!sender.Push(row.data(),row.size(),0,4,0));
 assert(!sender.Push(nullptr,row.size(),0,4,8));
 assert(!sender.Push(row.data(),row.size()-1,0,4,8));
}
