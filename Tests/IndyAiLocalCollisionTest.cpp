// SPDX-License-Identifier: GPL-3.0-or-later
#include "Network/Games/Indy500/AiLocalCollision.h"
#include <cassert>
#include <cmath>
#include <cstdio>
using namespace IndyAiLocalCollision;
static Actor A(unsigned i,unsigned owner,float x,float z,unsigned state=1) {
  return {IndyLocalCollision::ActorBase+i*IndyLocalCollision::ActorStride,i,i,owner,state,{x,z}};
}
int main() {
  Inputs s;
  auto self=A(2,LocalOwner,10,20), peer=A(3,1,30,40), own=A(4,LocalOwner,50,60);
  auto read=[&](uint32_t pc,const Actor& a,const Actor& b,unsigned n,uint64_t frame) {
    const auto site=Decode(pc);const auto& target=site.candidate?b:a;
    return s.Resolve(pc,target.base+site.field,n,a,b,frame);
  };
  assert(!read(0xa97a0,self,peer,0,100).substituted); // cross-cabinet unchanged
  assert(!read(0xa9790,self,peer,0,100).substituted);
  assert(!read(0xa9790,self,own,0,100).substituted); // both warm up together
  assert(!read(0xa97a0,self,own,0,100).substituted);
  self.live={11,22}; own.live={51,62};
  assert(read(0xa9790,self,own,0,101).value==51);
  assert(read(0xa9794,self,own,0,101).value==62);
  assert(read(0xa9798,self,own,0,101).value==50);
  assert(read(0xa979c,self,own,0,101).value==60);
  self.live={88,99}; own.live={80,90}; // both were latched on the first read
  assert(read(0xa97a0,self,own,0,101).value==11);
  assert(read(0xa97a4,self,own,0,101).value==22);
  assert(read(0xa97a8,self,own,0,101).value==10);
  assert(read(0xa97ac,self,own,0,101).value==20);
  assert(!read(0xa9790,self,peer,0,101).substituted);
  assert(!read(0xa97a0,self,peer,0,101).substituted);
  assert(!s.Resolve(0xa9790,own.base+0x12c,0,self,own,102).substituted);
  assert(!s.Resolve(0xdead,own.base+0x124,0,self,own,102).substituted);
  self.live={12,23}; own.live={52,63}; assert(read(0xa9790,self,own,0,102).value==52);
  own.live={53,64}; assert(!read(0xa9790,self,own,0,104).substituted); // gap
  own.live={54,65}; assert(read(0xa9790,self,own,0,105).substituted);
  own.live={90,90}; assert(!read(0xa9790,self,own,0,106).substituted); // warp
  own.live={91,91}; assert(read(0xa9790,self,own,0,107).substituted);
  own.live={NAN,0}; assert(!read(0xa9790,self,own,0,108).substituted);
  own.live={92,92}; assert(!read(0xa9790,self,own,0,109).substituted);
  assert(read(0xa9790,self,own,0,110).substituted);
  auto bad=own; bad.owner=1; assert(!read(0xa9790,self,bad,0,111).substituted);
  assert(!read(0xa9790,self,own,0,111).substituted); // ownership invalidates history
  bad=own;bad.identity=2;assert(!read(0xa9790,self,bad,0,112).substituted);
  bad=own;bad.state=2;assert(!read(0xa9790,self,bad,0,113).substituted);
  bad=own;bad.base+=4;assert(!read(0xa9790,self,bad,0,114).substituted);
  bad=A(0,LocalOwner,0,0);assert(!read(0xa97a0,bad,own,0,115).substituted); // player excluded
  assert(!read(0xa97a0,peer,own,0,116).substituted); // remote subject excluded
  assert(!read(0xa97a0,self,self,0,117).substituted);
  s.Clear();self.live={10,20};assert(!read(0xa97a0,self,own,0,118).substituted);

  // A previously warm subject paired with a fresh candidate must never use
  // one live actor alongside one native actor; both wait until frame 12.
  Inputs pair;
  auto a=A(2,LocalOwner,1,2), b=A(4,LocalOwner,3,4), c=A(6,LocalOwner,5,6);
  auto pairread=[&](uint32_t pc,const Actor& x,const Actor& y,uint64_t frame) {
    const auto site=Decode(pc); const auto& t=site.candidate?y:x;
    return pair.Resolve(pc,t.base+site.field,0,x,y,frame);
  };
  assert(!pairread(0xa9790,a,b,10).substituted);
  a.live={2,3};b.live={4,5};assert(pairread(0xa9790,a,b,11).substituted);
  assert(!pairread(0xa9790,a,c,11).substituted);
  assert(!pairread(0xa97a0,a,c,11).substituted);
  a.live={3,4};c.live={6,7};
  assert(pairread(0xa9790,a,c,12).substituted && pairread(0xa97a0,a,c,12).substituted);
  std::puts("PASS AI detector guards, coherent local-pair history and remote/player exclusions");
}
