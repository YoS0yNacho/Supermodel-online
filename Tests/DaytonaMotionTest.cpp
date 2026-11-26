// ROM-free tests for the original Daytona Rev A Online backup and draw path.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Network/Games/Daytona/Motion.h"
#include "Network/Games/Daytona/RelayBackup.h"
#include <cassert>
#include <iostream>
#include <limits>

using namespace DaytonaMotion;
namespace Backup=DaytonaOnline;
namespace Room=NativeRoomSettings;

static void SealBackup(uint8_t* p)
{
  Put(p+8,Backup::BackupChecksum(p),2);
}

static void BackupCases()
{
  std::array<uint8_t,0x4000> ram{};
  ram.fill(0xff);
  std::array<uint8_t,0x800> dpram;dpram.fill(0xff);
  const auto blankIo=dpram;
  auto backup=ram;
  assert(!Backup::ConfigureRelayBackup(ram.data(),ram.size(),dpram.data(),dpram.size(),2, 0,6) && ram==backup);
  auto* valid=ram.data()+Backup::BlockBytes;
  std::copy(Backup::Signature.begin(),Backup::Signature.end(),valid);
  std::fill(valid+10,valid+Backup::BlockBytes,0);
  valid[0x0b]=5;valid[0x0c]=1;valid[0x1c]=2;valid[0x20]=3;
  valid[0x11]=1;valid[0x33]=0x1c;valid[0x7c]=0x55;
  SealBackup(valid);assert(Backup::ValidBlock(valid));
  backup=ram;
  for (unsigned seat=1;seat<=8;++seat)
    for (unsigned d=0;d<4;++d)
      for (unsigned m=0;m<3;++m)
      {
        ram=backup;dpram=blankIo;
        const auto opts=Room::WithMode(Room::WithDifficulty(Room::Defaults(Backup::Game),d),m);
        assert(Backup::ConfigureRelayBackup(ram.data(),ram.size(),dpram.data(),dpram.size(),seat, 0,opts));
        auto ram60=backup;
        auto io60=blankIo;
        assert(Backup::ConfigureRelayBackup(ram60.data(),ram60.size(),io60.data(),io60.size(),seat, 0,opts | Room::Video60));
        assert(ram60==ram && io60==dpram);
        assert(Backup::ValidBlock(ram.data()) && std::equal(ram.begin(),ram.begin()+0x80,ram.begin()+0x80));
        assert(std::equal(ram.begin(),ram.begin()+0x80,dpram.begin()+Backup::IoOffset));
        assert(std::equal(dpram.begin(),dpram.begin()+0x100,blankIo.begin()));
        assert(std::equal(dpram.begin()+0x180,dpram.end(),blankIo.begin()+0x180));
        assert(ram[0x0b]==seat && ram[0x0c]==(seat!=1) && ram[0x1c]==m && ram[0x20]==d);
        for (unsigned i=0;i<0x80;++i)
          if (i!=8 && i!=9 && i!=0x0b && i!=0x0c && i!=0x1c && i!=0x20) assert(ram[i]==backup[i+0x80]);
        assert(std::equal(ram.begin()+0x100,ram.end(),backup.begin()+0x100));
      }
  ram=backup;
  for (unsigned options : {0u, 1u, 6u|8192u, 6u|2048u, 2u|(4u<<2), 6u|(3u<<5)})
    assert(!Backup::ConfigureRelayBackup(ram.data(),ram.size(),dpram.data(),dpram.size(),1, 0,options) && ram==backup);
  assert(!Backup::ConfigureRelayBackup(ram.data(),ram.size(),dpram.data(),dpram.size(),9, 0,6) && ram==backup);
  assert(!Backup::ConfigureRelayBackup(ram.data(),0xff,dpram.data(),dpram.size(),1, 0,6) && ram==backup);
  // Daytona flags No Collisions (256), Real Players (512), No Pitstop (4096) and a car number are accepted.
  ram=backup;dpram=blankIo;
  assert(Backup::ConfigureRelayBackup(ram.data(),ram.size(),dpram.data(),dpram.size(),2,5,6|256u|512u|4096u));
  assert(Backup::ValidBlock(ram.data()) && ram[0x7c]==5 && ram[0x20]==1);
  // A saved I/O record alone can recover bad backup RAM, but not if all
  // copies fail the signature/CRC. No unrelated I/O bytes are changed.
  ram.fill(0xff);
  assert(Backup::ConfigureRelayBackup(ram.data(),ram.size(),dpram.data(),dpram.size(),3, 0,6));
  ram=backup;dpram=blankIo;
  ram[0x80+0x20]^=1;
  assert(!Backup::ConfigureRelayBackup(ram.data(),ram.size(),dpram.data(),dpram.size(),1, 0,6));
}

static std::array<uint8_t,PoseBytes> Native(uint8_t tick,float x,float z,uint16_t yaw)
{
  std::array<uint8_t,PoseBytes> b{};
  b[0x0a]=tick;b[0x16]=0x12;b[0x1b]=b[0x1d]=0x16;
  uint32_t sum=0;
  for (unsigned i=0;i<0x3c;i+=4) sum+=uint32_t(Get(b.data()+i,4));
  Put(b.data()+0x3c,sum,4);
  Put(b.data()+0x5c,AsWord(x),4);Put(b.data()+0x64,AsWord(z),4);Put(b.data()+0x8e,yaw,2);
  return b;
}

static Row Car(uint8_t tick,float x,float z,uint16_t yaw,Scene s={1,0},unsigned seat=8)
{
  const auto b=Native(tick,x,z,yaw);
  return DecodeRow(b.data(),b.size(),seat,s);
}

static void DatagramCases()
{
  const auto a=Car(254,10,20,0x7ff0), b=Car(255,11,20,0x8000), c=Car(0,12,20,0x8010);
  assert(a && b && c && Ticks(b,c)==1 && Continuous(a,c));
  assert(!Continuous(a,Car(255,99,0,0)) && !Continuous(a,Car(255,11,20,0x8000,{2,0})));
  assert(std::abs(Turn(c.pose.yaw,a.pose.yaw))<.005);
  assert(!Car(1,std::numeric_limits<float>::infinity(),0,0));
  auto corrupt=Native(1,1,2,3);corrupt[0x14]^=1;
  assert(!DecodeRow(corrupt.data(),corrupt.size(),2,{1,0}));
  assert(!DecodeRow(corrupt.data(),16,2,{1,0}));
  assert(!Car(1,1,2,3,{},1) && !Car(1,1,2,3,{1,3},1) && !Car(1,1,2,3,{1,0},9));

  Publisher send;
  assert(send.Push(a)->n==1 && !send.Push(a));
  assert(send.Push(b)->n==2);
  auto wire=Encode(*send.Push(c));
  Packet p;
  assert(Decode(wire.data(),wire.size(),8,p) && p.seq==3 && p.n==3 && p.keys[2]==a.key);
  assert(!Decode(wire.data(),wire.size()-1,8,p) && !Decode(wire.data(),wire.size(),7,p));
  for (const auto offset : {0u, 4u, 12u, 15u, 26u, 32u, 48u, 56u})
  {
    auto bad=wire;
    if (offset==4 || offset==15 || offset==32 || offset==48)
      std::fill_n(bad.begin()+offset,offset>=32?8:1,0);
    else bad[offset]^=0xff;
    assert(!Decode(bad.data(),bad.size(),8,p));
  }
  Received r{p,1000,30000,3000};
  assert(Latest(a,r,2000).gap==2 && Latest(b,r,2000).gap==1);
  assert(!Latest(c,r,2000));
  assert(!Latest(a,r,152000));
  // Native key missing from UDP history: the fresh UDP pose is still a correction target.
  const auto fallback=Latest(Car(254,10,19,0x7ff0),r,2000);
  assert(fallback && fallback.row.key==r.p.row.key && fallback.gap>=1);
  r.uncertainty=80001;assert(!Latest(a,r,2000));
  const auto discontinuity=send.Push(Car(5,90,0,0,{2,0}));
  assert(discontinuity && discontinuity->seq==1 && discontinuity->n==1);
}

static void DrawCases()
{
  Predictor filter;Publisher publisher;
  const auto a=Car(254,0,0,0x7ff0), b=Car(255,1,0,0x8000), c=Car(0,2,0,0x8010);
  const auto d=Car(1,3,0,0x8014);
  publisher.Push(a);publisher.Push(b);publisher.Push(c);
  Received udp{*publisher.Push(d),100000,31000,3000};
  auto draw=filter.Step(a,a.pose,50,udp,1,103000);
  assert(draw.shown.p.x==0 && !draw.udp && !draw.weight);
  for (unsigned i=2;i<=5;++i)
  {
    draw=filter.Step(b,b.pose,50,udp,i,104000+i*1000);
    assert(draw.udp && draw.weight==i-1);
    assert((draw.shown.p-b.pose.p).Length()<=MaxShift && std::abs(Turn(draw.shown.yaw,b.pose.yaw))<=MaxYaw);
  }
  draw=filter.Step(b,b.pose,50,udp,6,350000);
  assert(!draw.udp && draw.weight==3);
  // Without UDP the displayed correction decays toward the native-coasted pose.
  double gap=1e9;
  for (unsigned i=7;i<=9;++i) {
    draw=filter.Step(b,b.pose,50,{},i,350000+i);
    const double next=(draw.shown.p-draw.tcp.p).Length();
    assert(next<=gap+1e-12 && (i<9 || !draw.weight));
    gap=next;
  }
  draw=filter.Step(c,c.pose,ContactRadius,udp,10,350100);
  assert(!draw.udp && !draw.weight && draw.shown.p.x==c.pose.p.x);
  draw=filter.Step(d,d.pose,ContactRadius+.5,{},11,350200);
  assert((draw.shown.p-d.pose.p).Length()<=.32*.5+1e-9);
  const auto next=Car(20,0,0,0x2000,{2,0});
  draw=filter.Step(next,next.pose,50,udp,12,350300);
  assert(!draw.udp && !draw.weight && draw.shown.p.x==next.pose.p.x && draw.shown.yaw==next.pose.yaw);
}

int main()
{
  BackupCases();DatagramCases();DrawCases();
  std::cout<<"PASS Daytona Rev A: mirrored backup, options/seat bounds, native row/UDP guards, native-first draw and fade\n";
}
