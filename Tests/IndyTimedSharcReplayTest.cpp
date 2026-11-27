// SPDX-License-Identifier: GPL-3.0-or-later
// ROM-free narrow tests for the scheduler diagnostic's supported bus regions.
#define INDY_TIMED_SHARC_REPLAY_NO_MAIN
#include "IndyTimedSharcReplay.cpp"
#include <cassert>
int main() {
  TimedBoard b; b.t.waits.assign(0x38,0);
  assert(b.Burst(0x0)); assert(b.Burst(0x200000)); assert(b.Burst(0x500000));
  assert(b.Burst(0x900000)); assert(b.Burst(0x97ffff));
  assert(b.Burst(0x2000000)); assert(b.Burst(0x3ffffff)); assert(b.Burst(0x6000000));
  assert(!b.Burst(0x884000)); assert(!b.Burst(0x980000));
  b.t.waits[0x00]=4; b.t.waits[0x08]=0x42; b.t.waits[0x04]=2; b.t.waits[0x34]=16;
  assert(b.areaWaits(0x100)==2); assert(b.areaWaits(0x500000)==0);
  assert(b.areaWaits(0x900000)==1); assert(b.areaWaits(0x2000000)==4);
  assert(b.areaWaits(0x884000)==0);
  CI960 cpu;b.cpu=&cpu;cpu.SetReg(CI960::REG_PIP,0xec70);
  bool rejected=false;try {b.get(0x53c7a0+0x124,4);} catch(const std::runtime_error &e) {
    rejected=std::string(e.what())=="Indy display-prediction state not captured";
  }
  assert(rejected);
  assert(b.Burst(0x1a121ca)); assert(!b.Burst(0x1a14000));
  cpu.SetReg(CI960::REG_PIP,0xa6538);b.commCaptured=true;b.commView.assign(0x4000,0);
  b.commView[0x21c8]=0x11;b.commView[0x21c9]=0x22;b.commView[0x21ca]=0x33;b.commView[0x21cb]=0x44;
  b.commEvents.push_back({'R',0xa6538,0x1a121c8,0xffff0000,0x44332211});
  assert(b.get(0x1a121ca,2)==0x4433); assert(b.commIndex==1);
  b.commEvents.push_back({'R',0xa6538,0x1a121c8,0x00ff0000,0x44332211});
  assert(b.get(0x1a121ca,1)==0x33);assert(b.commIndex==2);
  rejected=false;try {b.get(0x1a121ca,2);} catch(const std::runtime_error &e) {
    rejected=std::string(e.what())=="unexpected comm read";
  }
  assert(rejected);
  b.commIndex=0;b.commView[0x21ca]^=1;
  rejected=false;try {b.get(0x1a121ca,2);} catch(const std::runtime_error &e) {
    rejected=std::string(e.what())=="comm read transcript mismatch or asynchronous update";
  }
  assert(rejected);
  // Version 2 fixtures must be surrounded by witness markers; even an empty
  // or cleanly line-truncated transcript must not pass as an older fixture.
  char path[]="/tmp/indy-comm-parser-test-XXXXXX";int fd=mkstemp(path);assert(fd>=0);close(fd);
  const std::string begin="B 00000002 00000000 00000000 00000000\n";
  const std::string end="E 00000002 00000000 00000000 00000000\n";
  const std::string read="R 000a6538 01a121c8 00ff0000 44332211\n";
  const auto write=[&](const std::string &s){FILE *f=std::fopen(path,"wb");assert(f);assert(std::fwrite(s.data(),1,s.size(),f)==s.size());assert(!std::fclose(f));};
  write(begin+read+end);auto events=readComm(path,true);assert(events.size()==1 && events[0].value==0x44332211);
  for(const auto &bad:{std::string(),begin,read+end,begin+read,begin+end+read,
                        begin+"X 00000000 00000001 00000000 00000000\n"+end,
                        begin+"E 00000003 00000000 00000000 00000000\n"}) {
    write(bad);rejected=false;try{readComm(path,true);}catch(const std::runtime_error &){rejected=true;}assert(rejected);
  }
  write(read);assert(readComm(path,false).size()==1);
  write(begin+end);rejected=false;try{readComm(path,false);}catch(const std::runtime_error &){rejected=true;}assert(rejected);
  std::remove(path);
  std::puts("timed replay bus and comm witness checks passed");
}
