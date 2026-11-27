// SPDX-License-Identifier: GPL-3.0-or-later
// Experimental one-routine i960 fixture replay. Requires a privately captured
// fixture, the user's own matching Indy program bytes and the Supermodel source.
// Not a full emulator: rejects all unmapped bus access, devices and interrupts.
#include "CPU/I960/I960.h"
#include "BlockFile.h"
#include "Types.h"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <unistd.h>

void DebugLog(const char *, ...) {}
Result ErrorLog(const char *fmt, ...) {
  va_list a; va_start(a,fmt); std::vfprintf(stderr,fmt,a); va_end(a); return Result::FAIL;
}
struct Meta {
  uint32_t version=0,actor=0,returnPC=0,valid=0,reset=0;
  uint64_t frame=0;
  float x=0,z=0,previousX=0,previousZ=0;
};
static void require(bool ok,const char *why) { if (!ok) throw std::runtime_error(why); }
static std::vector<uint8_t> bytes(const std::string &p) {
  std::ifstream f(p,std::ios::binary); require(bool(f),"unable to open file");
  return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};
}
static void block(CBlockFile &f,const char *name,std::vector<uint8_t>& dest) {
  require(f.FindBlock(name)==Result::OKAY,"missing RAM block");
  uint32_t n=0; require(f.Read(n)==sizeof(n) && n<=0x100000 && n>0,"invalid RAM size");
  dest.resize(n); require(f.Read(dest.data(),n)==n,"truncated RAM block");
}
static Meta metadata(CBlockFile &f) {
  Meta m; require(f.FindBlock("Fixture metadata")==Result::OKAY,"missing metadata");
  require(f.Read(m.version)==4 && f.Read(m.frame)==8 && f.Read(m.actor)==4 &&
    f.Read(m.returnPC)==4 && f.Read(m.valid)==4 && f.Read(m.reset)==4 &&
    f.Read(m.x)==4 && f.Read(m.z)==4 && f.Read(m.previousX)==4 && f.Read(m.previousZ)==4,
    "truncated metadata");
  require(m.version==1 && m.actor<2 && m.valid==1 && m.reset<=1 && m.returnPC>0 && m.returnPC<0x200000 &&
          std::isfinite(m.x) && std::isfinite(m.z) && std::isfinite(m.previousX) && std::isfinite(m.previousZ),"invalid metadata");
  return m;
}
static uint32_t word(const std::vector<uint8_t> &v,size_t n) {
  require(n+4<=v.size(),"word out of bounds");
  return uint32_t(v[n]) | uint32_t(v[n+1])<<8 | uint32_t(v[n+2])<<16 | uint32_t(v[n+3])<<24;
}
struct IO {char kind; uint32_t pc,address,mask,value,empty;};
static std::vector<IO> readIO(const std::string &p) {
  const auto data=bytes(p);
  require(data.size()<10000000 && (data.empty() || data.back()=='\n'),"truncated or oversized transcript");
  std::istringstream input(std::string(data.begin(),data.end()));
  std::vector<IO> result; std::string line;
  while (std::getline(input,line)) {
    IO x{}; std::istringstream row(line); std::string extra;
    require(bool(row >> x.kind >> std::hex >> x.pc >> x.address >> x.mask >> x.value >> std::dec >> x.empty) && !(row >> extra),"malformed transcript event");
    require((x.kind=='R'||x.kind=='W') && x.mask==0xffffffff && x.empty<=1 && !(x.kind=='W' && x.empty),"invalid transcript event");
    result.push_back(x);
  }
  return result;
}
struct Bus : II960Bus {
  CI960 *cpu=nullptr; std::vector<uint8_t> rom, dataRom, main, work; Meta meta;
  std::vector<IO> io; size_t event=0;
  bool experiment=false; unsigned substitutions=0,reads=0,writes=0;
  IO take(char kind,uint32_t addr,uint32_t data=0) {
    if (event>=io.size()) throw std::runtime_error("coprocessor transcript exhausted");
    const IO x=io[event++];
    if (x.kind!=kind || x.address!=addr || x.pc!=cpu->GetPIP() || (kind=='W' && x.value!=data)) {
      char b[250]; std::snprintf(b,sizeof b,"coprocessor mismatch at event %zu: got %c pc=%08x addr=%08x data=%08x; recorded %c pc=%08x addr=%08x data=%08x",event-1,kind,cpu->GetPIP(),addr,data,x.kind,x.pc,x.address,x.value);
      throw std::runtime_error(b);
    }
    return x;
  }
  std::vector<uint8_t> &map(uint32_t a,size_t n,size_t &off) {
    if (a<rom.size() && uint64_t(a)+n<=rom.size()) {off=a;return rom;}
    if (a>=0x2000000 && uint64_t(a-0x2000000)+n<=dataRom.size()) {off=a-0x2000000;return dataRom;}
    if (a>=0x6000000 && a<0x7000000 && uint64_t(a-0x6000000)+0x1000000+n<=dataRom.size()) {off=a-0x6000000+0x1000000;return dataRom;}
    if (a>=0x200000 && uint64_t(a-0x200000)+n<=main.size()) {off=a-0x200000;return main;}
    if (a>=0x500000 && uint64_t(a-0x500000)+n<=work.size()) {off=a-0x500000;return work;}
    char b[160]; std::snprintf(b,sizeof b,"unsupported bus access %08x pc=%08x",a,cpu?cpu->GetPIP():0); throw std::runtime_error(b);
  }
  uint32_t rd(uint32_t a,unsigned n) {
    if (a>=0x884000 && a<0x888000) {
      require(n==4,"unexpected narrow coprocessor read"); ++reads;
      auto x=take('R',a); if (x.empty) cpu->Stall(); return x.value;
    }
    size_t off; auto &v=map(a,n,off); uint32_t x=0; for(unsigned i=0;i<n;++i)x|=uint32_t(v[off+i])<<(8*i); ++reads;
    if (experiment && n==4) {
      const auto pc=cpu->GetPIP();
      bool xx=pc==0xac4e8 || pc==0xac504 || pc==0xac2ac || pc==0xac2c4;
      bool zz=pc==0xac4fc || pc==0xac510 || pc==0xac2bc || pc==0xac2cc;
      bool history=pc==0xac504 || pc==0xac510 || pc==0xac2c4 || pc==0xac2cc;
      uint32_t field=history?(xx?0x144:0x148):(xx?0x124:0x12c);
      if ((xx||zz) && a==0x53c7a0+meta.actor*0x1e4+field) {
        float f=history?(xx?meta.previousX:meta.previousZ):(xx?meta.x:meta.z);
        std::memcpy(&x,&f,4); ++substitutions;
      }
    }
    return x;
  }
  void wr(uint32_t a,unsigned n,uint32_t value) {
    if (a>=0x880000 && a<0x888000) {
      require(n==4,"unexpected narrow coprocessor write"); take('W',a,value); ++writes; return;
    }
    size_t off; auto &v=map(a,n,off); require(&v!=&rom && &v!=&dataRom,"unexpected ROM write");
    for(unsigned i=0;i<n;++i) v[off+i]=uint8_t(value>>(8*i));
    ++writes;
  }
  UINT8 Read8(UINT32 a) override {return rd(a,1);}
  UINT16 Read16(UINT32 a) override {return rd(a,2);}
  UINT32 Read32(UINT32 a) override {return rd(a,4);}
  void Write8(UINT32 a,UINT8 v) override {wr(a,1,v);}
  void Write16(UINT32 a,UINT16 v) override {wr(a,2,v);}
  void Write32(UINT32 a,UINT32 v) override {wr(a,4,v);}
  bool Burst(UINT32 a) override {return a<0x240000 || (a>=0x500000 && a<0x600000);}
};
static std::vector<uint8_t> cpuBytes(CI960 &cpu,const std::string &p) {
  CBlockFile f; require(f.Create(p,"Replay CPU","lab")==Result::OKAY,"CPU output create failed");
  cpu.SaveState(&f,"Fixture i960"); f.Close(); return bytes(p);
}
struct Run {std::vector<uint8_t> main,work,cpu;uint32_t g0;unsigned reads,writes,substitutions,instructions;};
static Run replay(const std::string &in,const std::vector<uint8_t>& rom,const std::vector<uint8_t>& dataRom,const std::vector<IO>& io,bool experimental,const std::string &tmp) {
  CBlockFile f; require(f.Load(in)==Result::OKAY,"invalid fixture"); Meta m=metadata(f);
  Bus b; b.rom=rom; b.dataRom=dataRom; b.meta=m; b.experiment=experimental; b.io=io;
  block(f,"Fixture main RAM",b.main); block(f,"Fixture work RAM",b.work);
  require(b.rom.size()==0x200000 && b.dataRom.size()==0x2000000 && b.main.size()==0x40000 && b.work.size()==0x100000,"unexpected memory sizes");
  require(word(b.rom,0xac4e8)==0x90b43400 && word(b.rom,0xac4fc)==0x90bc3400 &&
    word(b.rom,0xac504)==0x90ac3400 && word(b.rom,0xac510)==0x908c3400 &&
    word(b.rom,0xac2ac)==0x90bd3400 && word(b.rom,0xac2bc)==0x90b53400 &&
    word(b.rom,0xac2c4)==0x908d3400 && word(b.rom,0xac2cc)==0x90953400,"wrong ROM revision");
  CI960 cpu; b.cpu=&cpu; cpu.AttachBus(&b); cpu.SetFetchRegion(0,b.rom.size(),b.rom.data());
  cpu.SetFetchRegion(0x200000,b.main.size(),b.main.data()); cpu.SetFetchRegion(0x500000,b.work.size(),b.work.data());
  require(f.FindBlock("Fixture i960")==Result::OKAY,"missing captured CPU block");
  cpu.LoadState(&f,"Fixture i960"); f.Close();
  require(cpu.GetIP()==0xac230 || cpu.GetIP()==0xac264,"fixture is not a supported routine boundary");
  require(cpu.GetIP()!=m.returnPC,"fixture entry equals return boundary");
  unsigned insn=0; bool arrived=false;
  cpu.SetTraceCallback([&](uint32_t){
    if (++insn>1000000) throw std::runtime_error("instruction limit exceeded");
  });
  cpu.SetInstructionGate(m.returnPC,[&](){arrived=true; return false;});
  for (unsigned slices=0; !arrived && slices<10000; ++slices) cpu.Run(1000000);
  require(arrived && !cpu.IsFatalHalted(),"replay did not reach recorded return boundary");
  require(b.event==b.io.size(),"replay did not consume complete coprocessor transcript");
  return {b.main,b.work,cpuBytes(cpu,tmp),cpu.GetReg(16),b.reads,b.writes,b.substitutions,insn};
}
static size_t changed(const std::vector<uint8_t> &a,const std::vector<uint8_t>&b) {
  require(a.size()==b.size(),"memory length differs");size_t n=0;for(size_t i=0;i<a.size();++i)n+=a[i]!=b[i];return n;
}
#ifndef INDY_REPLAY_NO_MAIN
int main(int argc,char **argv) {
  if(argc!=6) {std::fprintf(stderr,"usage: %s fixture.in fixture.out fixture-program.bin fixture-data.bin fixture.io\n",argv[0]); return 2;}
  try {
    auto rom=bytes(argv[3]); auto dataRom=bytes(argv[4]); auto io=readIO(argv[5]);
    char tempName[]="/tmp/indy-replay-cpu-XXXXXX";
    int fd=mkstemp(tempName); require(fd>=0,"unable to create temporary file"); close(fd);
    const std::string temp=tempName;
    struct Cleanup { std::string path; ~Cleanup() {std::remove(path.c_str());} } cleanup{temp};
    auto exp=replay(argv[1],rom,dataRom,io,true,temp);
    auto exp2=replay(argv[1],rom,dataRom,io,true,temp);
    CBlockFile out; require(out.Load(argv[2])==Result::OKAY,"missing captured result");
    std::vector<uint8_t> expectedMain,expectedWork; block(out,"Fixture main RAM",expectedMain);block(out,"Fixture work RAM",expectedWork);
    require(out.FindBlock("Fixture i960")==Result::OKAY,"missing expected CPU block");
    CI960 expected; expected.LoadState(&out,"Fixture i960"); out.Close(); auto expectedCPU=cpuBytes(expected,temp);
    const auto actualWork=changed(exp.work,expectedWork), actualMain=changed(exp.main,expectedMain);
    const bool actualCPU=(exp.cpu==expectedCPU);
    const bool deterministic=exp.work==exp2.work && exp.main==exp2.main && exp.cpu==exp2.cpu;
    std::printf("replay experiment: instructions=%u reads=%u writes=%u substitutions=%u g0=%08x flag=%08x\n",exp.instructions,exp.reads,exp.writes,exp.substitutions,exp.g0,word(exp.work,0x440b0));
    std::printf("captured comparison: work_changed_bytes=%zu main_changed_bytes=%zu cpu_equal=%d deterministic=%d\n",actualWork,actualMain,actualCPU,deterministic);
    if (actualWork || actualMain || !actualCPU || !deterministic) return 1;
    try {
      auto native=replay(argv[1],rom,dataRom,io,false,temp);
      std::printf("native vs experimental (same transcript): work_changed_bytes=%zu main_changed_bytes=%zu cpu_equal=%d native_g0=%08x native_flag=%08x\n",changed(native.work,exp.work),changed(native.main,exp.main),native.cpu==exp.cpu,native.g0,word(native.work,0x440b0));
      unsigned displayed=0;
      for (size_t i=0; i+4<=native.work.size(); i+=4) {
        auto a=word(native.work,i), b=word(exp.work,i);
        if (a!=b && displayed++<16)
          std::printf("  work %08zx native=%08x experiment=%08x\n",i+0x500000,a,b);
      }
      if (displayed>16) std::printf("  ... %u changed words total\n",displayed);
    } catch(const std::exception &e) {
      std::printf("native replay inconclusive: %s\n",e.what());
    }
    return 0;
  } catch(const std::exception &e) {std::fprintf(stderr,"replay failed: %s\n",e.what());return 1;}
}
#endif
