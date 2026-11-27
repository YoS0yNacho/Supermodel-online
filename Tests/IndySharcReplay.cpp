// SPDX-License-Identifier: GPL-3.0-or-later
// Experimental asynchronous i960/SHARC detector replay; NOT the Model 2 scheduler.
// Reuses the strict fixture loader and i960 bus from IndyCollisionReplay.cpp.
#define INDY_REPLAY_NO_MAIN
#include "IndyCollisionReplay.cpp"
#include "CPU/SHARC/SHARC.h"
#include <deque>

struct Board : Bus, ISHARCBus {
  CSHARC sharc;
  std::vector<uint8_t> copro;
  std::vector<uint32_t> buffer;
  std::deque<uint32_t> in, out;
  std::vector<IO> observed;
  unsigned capacity=16, sharcIns=0, cpuStalls=0, cpuIns=0;
  bool waiting=false;
  void flags() { sharc.SetFlagInput(0,in.empty()); sharc.SetFlagInput(1,out.size()>=capacity); }
  void push(uint32_t x) {
    require(in.size()<capacity,"input FIFO full: simplified scheduler cannot model overflow");
    in.push_back(x); flags();
  }
  UINT32 Read32(UINT32 a) override {
    if(a>=0x884000 && a<0x888000) {
      if(out.empty()) {waiting=true; ++cpuStalls; cpu->Stall(); return 0;}
      uint32_t x=out.front(); out.pop_front(); flags(); waiting=false;
      observed.push_back({'R',cpu->GetPIP(),a,0xffffffff,x,0}); return x;
    }
    return Bus::Read32(a);
  }
  void Write32(UINT32 a,UINT32 x) override {
    if(a>=0x880000 && a<0x884000) {
      observed.push_back({'W',cpu->GetPIP(),a,0xffffffff,x,0});
      push((x&0x800fffff)|((((a&0x3fff)>>4)&0xff)<<23)); return;
    }
    if(a>=0x884000 && a<0x888000) {
      observed.push_back({'W',cpu->GetPIP(),a,0xffffffff,x,0}); push(x); return;
    }
    Bus::Write32(a,x);
  }
  UINT32 ReadData(UINT32 a) override {
    if (a>=0x400000 && a<0xc00000) {
      if (in.empty()) return 0; // original 2B SHARC polls empty flag
      uint32_t x=in.front(); in.pop_front(); flags(); return x;
    }
    if(a>=0x1400000 && a<0x1c00000) return buffer[a&0x7fff];
    if(a>=0x1c00000 && a<0x1e00000) return word(copro,((a-0x1c00000)&0x1fffff)*4);
    char b[100];std::snprintf(b,sizeof b,"unsupported SHARC data read %08x pc=%08x",a,sharc.GetPC());throw std::runtime_error(b);
  }
  void WriteData(UINT32 a,UINT32 x) override {
    if(a>=0xc00000 && a<0x1400000) {
      require(out.size()<capacity,"output FIFO full: simplified scheduler cannot model overflow");
      out.push_back(x); flags();
      if(waiting) sharc.EndSlice();
      return;
    }
    if(a>=0x1400000 && a<0x1c00000) {buffer[a&0x7fff]=x; return;}
    char b[100];std::snprintf(b,sizeof b,"unsupported SHARC data write %08x pc=%08x",a,sharc.GetPC());throw std::runtime_error(b);
  }
  UINT64 ReadProgram(UINT32 a) override {char b[100];std::snprintf(b,sizeof b,"unsupported SHARC program read %08x",a);throw std::runtime_error(b);}
  void WriteProgram(UINT32 a,UINT64) override {char b[100];std::snprintf(b,sizeof b,"unsupported SHARC program write %08x",a);throw std::runtime_error(b);}
};
struct DeviceMeta { int64_t i960Time=0,sharcTime=0,lineEnd=0; };
static DeviceMeta loadDevices(CBlockFile &f,Board &b) {
  require(f.FindBlock("Fixture devices")==Result::OKAY,"missing fixture device snapshot");
  uint32_t version=0,n=0,ctl=0; DeviceMeta d;
  uint8_t halted[2]{},empty[2]{},full[2]{}; bool held=false;
  require(f.Read(version)==4 && version==1 && f.Read(d.i960Time)==8 && f.Read(d.sharcTime)==8 && f.Read(d.lineEnd)==8 &&
          f.Read(halted)==2 && f.Read(empty)==2 && f.Read(full)==2 && f.Read(held)==1 && f.Read(ctl)==4,"invalid device metadata");
  require(!halted[0] && !held && !(ctl&0x80000000),"unsupported halted or upload state");
  auto fifo=[&](std::deque<uint32_t> &q) {
    uint32_t limit=0,extras=0;
    require(f.Read(limit)==4 && limit==16 && f.Read(n)==4 && n<=limit,"invalid fifo");
    for(unsigned i=0;i<n;++i) {uint32_t v=0;require(f.Read(v)==4,"truncated fifo");q.push_back(v);}
    require(f.Read(extras)==4 && extras==0,"simplified scheduler does not support extra fifo entries");
  };
  fifo(b.in); fifo(b.out);
  require(f.Read(n)==4 && n==0x8000,"unexpected buffer length");
  b.buffer.resize(n);require(f.Read(b.buffer.data(),n*4)==n*4,"truncated buffer");
  require(f.FindBlock("Fixture SHARC")==Result::OKAY,"missing SHARC state");
  b.sharc.AttachBus(&b);b.sharc.LoadState(&f,"Fixture SHARC");
  require(!b.sharc.IsHalted(),"SHARC halted");
  return d;
}
struct Outcome {Run cpu;std::vector<IO> events;std::vector<uint32_t> buffer;std::vector<uint8_t> sharc;unsigned sharcInstructions,stalls,sharcPC,tailSteps;bool tailAligned;std::deque<uint32_t> in,out;};
static std::vector<uint8_t> sharcBytes(CSHARC &cpu,const std::string &p) {
  CBlockFile f; require(f.Create(p,"Replay SHARC","lab")==Result::OKAY,"SHARC output create failed");
  cpu.SaveState(&f,"Fixture SHARC");f.Close();return bytes(p);
}
static Outcome execute(const std::string &input,const std::vector<uint8_t>&rom,const std::vector<uint8_t>&data,const std::vector<uint8_t>&copro,bool experiment,const std::string &tmp, const Board *target=nullptr,const std::vector<uint8_t> *targetState=nullptr) {
  CBlockFile f;require(f.Load(input)==Result::OKAY,"invalid fixture"); Meta m=metadata(f);
  Board b; b.rom=rom;b.dataRom=data;b.copro=copro;b.meta=m;b.experiment=experiment;
  require(rom.size()==0x200000 && data.size()==0x2000000 && copro.size()==0x800000,"wrong ROM sizes");
  require(word(rom,0xac4e8)==0x90b43400 && word(rom,0xac4fc)==0x90bc3400 &&
          word(rom,0xac504)==0x90ac3400 && word(rom,0xac510)==0x908c3400 &&
          word(rom,0xac2ac)==0x90bd3400 && word(rom,0xac2bc)==0x90b53400 &&
          word(rom,0xac2c4)==0x908d3400 && word(rom,0xac2cc)==0x90953400,"wrong ROM revision");
  block(f,"Fixture main RAM",b.main);block(f,"Fixture work RAM",b.work);
  require(b.main.size()==0x40000 && b.work.size()==0x100000,"wrong RAM sizes");
  loadDevices(f,b);
  CI960 cpu;b.cpu=&cpu;cpu.AttachBus(&b);cpu.SetFetchRegion(0,b.rom.size(),b.rom.data());
  cpu.SetFetchRegion(0x200000,b.main.size(),b.main.data());cpu.SetFetchRegion(0x500000,b.work.size(),b.work.data());
  require(f.FindBlock("Fixture i960")==Result::OKAY,"missing CPU state");cpu.LoadState(&f,"Fixture i960");f.Close();
  require(cpu.GetIP()==0xac230 && m.returnPC!=0xac230,"requires a pre-selection fixture");
  bool done=false;
  cpu.SetInstructionGate(m.returnPC,[&](){done=true;return false;});
  cpu.SetTraceCallback([&](uint32_t){if(++b.cpuIns>1000000)throw std::runtime_error("i960 instruction limit");});
  b.sharc.SetTraceCallback([&](uint32_t){if(++b.sharcIns>2000000)throw std::runtime_error("SHARC instruction limit");});
  for(unsigned slices=0; !done && slices<2000; ++slices) {
    if (b.waiting) {
      for (unsigned spins=0; b.out.empty() && spins<100; ++spins) {
        b.sharc.Run(10000); require(!b.sharc.IsFatalHalted(),"SHARC fatal");
      }
      require(!b.out.empty(),"SHARC did not produce a result within budget");
      b.waiting=false;
    }
    cpu.Run(1000000); require(!cpu.IsFatalHalted(),"i960 fatal");
  }
  require(done,"return boundary not reached");
  Run r{b.main,b.work,cpuBytes(cpu,tmp),cpu.GetReg(16),b.reads,b.writes,b.substitutions,b.cpuIns};
  // Diagnostic only: advance SHARC after the i960 boundary to see whether the
  // observed mismatch is trailing queued work. This is NOT timed scheduling.
  unsigned tail=0; bool aligned=false;
  if(target && targetState) {
    for(;tail<10000;++tail) {
      if(b.sharc.GetPC()==target->sharc.GetPC() && b.in==target->in && b.out==target->out &&
         b.buffer==target->buffer && sharcBytes(b.sharc,tmp)==*targetState) {aligned=true;break;}
      b.sharc.Run(1); require(!b.sharc.IsFatalHalted(),"SHARC fatal during diagnostic tail");
    }
  }
  return {r,b.observed,b.buffer,sharcBytes(b.sharc,tmp),b.sharcIns,b.cpuStalls,b.sharc.GetPC(),tail,aligned,b.in,b.out};
}
static bool sameEvents(const std::vector<IO>&recorded,const std::vector<IO>&observed) {
  std::vector<IO> valid;for(auto x:recorded) if(!x.empty) valid.push_back(x);
  if (valid.size()!=observed.size()) return false;
  for(size_t i=0;i<valid.size();++i) {
    const auto a=valid[i],b=observed[i];
    if(a.kind!=b.kind || a.pc!=b.pc || a.address!=b.address || a.value!=b.value || a.mask!=b.mask) {
      std::printf("event mismatch %zu: actual %c %08x %08x %08x expected %c %08x %08x %08x\n",i,b.kind,b.pc,b.address,b.value,a.kind,a.pc,a.address,a.value);
      return false;
    }
  }
  return true;
}
#ifndef INDY_SHARC_REPLAY_NO_MAIN
int main(int argc,char **argv) {
  if(argc!=7 && argc!=8) {std::fprintf(stderr,"usage: %s fixture.in fixture.out program.bin main_data.bin copro_data.bin fixture.io [--explore-native]\n",argv[0]);return 2;}
  try {
    require(argc==7 || std::string(argv[7])=="--explore-native","unknown option");
    auto rom=bytes(argv[3]), data=bytes(argv[4]),copro=bytes(argv[5]);auto transcript=readIO(argv[6]);
    char filename[]="/tmp/indy-sharc-replay-XXXXXX";int fd=mkstemp(filename);require(fd>=0,"temp failed");close(fd);
    struct Cleanup{std::string p;~Cleanup(){std::remove(p.c_str());}}cleanup{filename};std::string tmp=filename;
    CBlockFile f;require(f.Load(argv[2])==Result::OKAY,"invalid output fixture");
    std::vector<uint8_t> main,work;block(f,"Fixture main RAM",main);block(f,"Fixture work RAM",work);
    CI960 expected;require(f.FindBlock("Fixture i960")==Result::OKAY,"missing expected CPU");expected.LoadState(&f,"Fixture i960");auto ecpu=cpuBytes(expected,tmp);
    Board expectedBoard; auto endTime=loadDevices(f,expectedBoard);
    auto es=sharcBytes(expectedBoard.sharc,tmp);f.Close();
    auto exp=execute(argv[1],rom,data,copro,true,tmp,&expectedBoard,&es);
    bool events=sameEvents(transcript,exp.events);size_t md=changed(main,exp.cpu.main),wd=changed(work,exp.cpu.work);
    bool ce=ecpu==exp.cpu.cpu, se=es==exp.sharc;
    std::printf("EXPERIMENT experimental_cpu_equal=%d main_changed_bytes=%zu work_changed_bytes=%zu nonstall_fifo_events_equal=%d sharc_equal_after_tail=%d i960_instructions=%u sharc_instructions=%u stalls=%u\n",ce,md,wd,events,se,exp.cpu.instructions,exp.sharcInstructions,exp.stalls);
    std::printf("DEVICE actual_pc=%08x expected_pc=%08x in_equal=%d out_equal=%d buffer_equal=%d sharc_snapshot_byte_differences=%zu expected_cpu_clock=%lld expected_sharc_clock=%lld\n",exp.sharcPC,expectedBoard.sharc.GetPC(),exp.in==expectedBoard.in,exp.out==expectedBoard.out,exp.buffer==expectedBoard.buffer,changed(es,exp.sharc),(long long)endTime.i960Time,(long long)endTime.sharcTime);
    std::printf("DIAGNOSTIC_TAIL aligned=%d steps=%u (post-boundary convergence is NOT scheduler validation)\n",exp.tailAligned,exp.tailSteps);
    if(argc==8 && ce && !md && !wd && events && exp.tailAligned) {
      try {
        auto native=execute(argv[1],rom,data,copro,false,tmp);
        std::printf("EXPLORATORY_UNVALIDATED native_g0=%08x experiment_g0=%08x native_flag=%08x experiment_flag=%08x work_changed_bytes=%zu\n",native.cpu.g0,exp.cpu.g0,word(native.cpu.work,0x440b0),word(exp.cpu.work,0x440b0),changed(native.cpu.work,exp.cpu.work));
      } catch(const std::exception &e) {std::printf("EXPLORATORY_NATIVE_FAILED: %s\n",e.what());}
    }
    std::puts("INCONCLUSIVE: asynchronous execution is not a verified Model 2 scheduler");
    return 1;

  } catch(const std::exception &e) {std::fprintf(stderr,"INCONCLUSIVE: %s\n",e.what());return 1;}
}
#endif
