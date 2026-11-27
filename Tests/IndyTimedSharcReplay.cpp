// SPDX-License-Identifier: GPL-3.0-or-later
// Narrow scheduler diagnostic, not an emulator or a validated counterfactual.
// Only v2 private fixtures, idle timers, no delayed callback, no FIFO overflow,
// no vblank/frame crossing, and no external UART traffic are supported.
#define INDY_SHARC_REPLAY_NO_MAIN
#include "IndySharcReplay.cpp"
#include "Model1/I8251.h"

// CI960 serialization below is revision- and ABI-specific; never silently
// interpret a different layout or allow a block read to spill into the next.
static_assert(sizeof(int)==4 && sizeof(double)==8 && sizeof(bool)==1,"unsupported fixture ABI");
static void validateFixtureLayout(const std::string &path) {
  auto data=bytes(path);require(data.size()<=16*1024*1024,"oversized fixture");
  size_t at=0;
  std::vector<std::string> names;
  while(at<data.size()) {
    require(data.size()-at>=12,"truncated block header");
    uint32_t len=word(data,at), n=word(data,at+4), c=word(data,at+8);
    require(len>=12 && len<=data.size()-at && n && c && uint64_t(12)+n+c<=len,"invalid block length");
    size_t start=at+12;
    require(data[start+n-1]==0 && data[start+n+c-1]==0,"invalid block strings");
    std::string name(reinterpret_cast<const char*>(data.data()+start),n-1);
    require(name.find('\0')==std::string::npos && std::find(names.begin(),names.end(),name)==names.end(),"duplicate or invalid block name");
    names.push_back(name);
    size_t payload=start+n+c, end=at+len, size=end-payload;
    if(name=="Fixture metadata")require(size==44,"unexpected metadata block length");
    if(name=="Fixture main RAM")require(size==0x40004,"unexpected main RAM block length");
    if(name=="Fixture work RAM")require(size==0x100004,"unexpected work RAM block length");
    if(name=="Fixture i960")require(size==499,"unexpected i960 block length");
    if(name=="Fixture SHARC")require(size==264129,"unexpected SHARC block length");
    if(name=="Fixture timing")require(size==166 && word(data,payload)==2,"requires v2 timing fixture");
    if(name=="Fixture comm view")require(size==0x4008 && (word(data,payload)==1||word(data,payload)==2) && word(data,payload+4)==0x4000,"invalid comm view block");
    if(name=="Model 2 comm")require(size>=0x4000 && size<=0x200000,"invalid comm state block");
    if(name=="Fixture UART") {
      require(size>=60,"truncated UART block");
      uint32_t queued=word(data,payload+56);
      require(queued<=0x10000 && size==60+size_t(queued),"invalid UART queue length");
    }
    if(name=="Fixture devices") {
      require(size>=39,"truncated device header");size_t p=payload+39;
      for(int q=0;q<2;++q) {
        require(end-p>=8,"truncated FIFO header");uint32_t cap=word(data,p),count=word(data,p+4);p+=8;
        require(cap==16 && count<=cap && end-p>=uint64_t(count)*4+4,"invalid FIFO length");p+=size_t(count)*4;
        uint32_t extra=word(data,p);p+=4;require(extra==0,"FIFO extras unsupported");
      }
      require(end-p>=4 && word(data,p)==0x8000 && end-p==4+0x8000*4,"invalid buffer length");
    }
    at+=len;
  }
  for(const char *name:{"Fixture metadata","Fixture main RAM","Fixture work RAM","Fixture i960","Fixture SHARC","Fixture devices","Fixture timing","Fixture UART"})
    require(std::find(names.begin(),names.end(),name)!=names.end(),"missing required fixture block");
  bool view=std::find(names.begin(),names.end(),"Fixture comm view")!=names.end();
  bool raw=std::find(names.begin(),names.end(),"Model 2 comm")!=names.end();
  require(view==raw,"incomplete comm snapshot");
}
// Call after validateFixtureLayout; returns an empty vector only if absent.
static std::vector<uint8_t> fixturePayload(const std::string &path,const char *wanted) {
  auto data=bytes(path);size_t at=0;
  while(at<data.size()) {
    uint32_t len=word(data,at),n=word(data,at+4),c=word(data,at+8);
    size_t start=at+12;
    if(std::string(reinterpret_cast<const char*>(data.data()+start),n-1)==wanted)
      return {data.begin()+start+n+c,data.begin()+at+len};
    at+=len;
  }
  return {};
}
struct CommEvent {char kind;uint32_t pc,address,mask,value;};
static std::vector<CommEvent> readComm(const std::string &p,bool requireWitness) {
  auto data=bytes(p);
  require(data.size()<10000000 && (data.empty()||data.back()=='\n'),"truncated or oversized comm transcript");
  std::istringstream input(std::string(data.begin(),data.end()));std::vector<CommEvent> result;std::string line;
  bool began=false,ended=false;size_t lineNumber=0;
  while(std::getline(input,line)) {
    CommEvent x{};std::istringstream row(line);std::string extra;++lineNumber;
    require(bool(row>>x.kind>>std::hex>>x.pc>>x.address>>x.mask>>x.value) && !(row>>extra),"malformed comm event");
    require(!ended,"comm transcript has data after witness end");
    if(x.kind=='B' || x.kind=='E') {
      require(requireWitness && x.pc==2 && x.address==0 && x.mask==0 && x.value==0,"invalid comm witness marker");
      if(x.kind=='B'){require(lineNumber==1 && !began,"invalid comm witness begin");began=true;}
      else {require(began,"comm witness end without begin");ended=true;}
      continue;
    }
    if(requireWitness) require(began,"missing comm witness begin");
    require(x.kind!='X',"unsupported comm mutation/control entry witnessed");
    bool mask=x.mask==0xff||x.mask==0xff00||x.mask==0xff0000||x.mask==0xff000000||x.mask==0xffff||x.mask==0xffff0000||x.mask==0xffffffff;
    require((x.kind=='R'||x.kind=='W') && x.address>=0x1a00000 && x.address<0x1a20000 && (x.address&0xffff)<0x4000 && !(x.address&3) && mask,"invalid comm event");
    result.push_back(x);
  }
  require(!requireWitness || (began && ended),"incomplete comm witness");
  return result;
}
struct Timing {
  std::vector<uint8_t> waits;
  uint32_t timerVal[4]{},timerOrig[4]{},irqReq=0,irqEna=0;
  uint8_t timerRun[4]{};
  int64_t timerStart[4]{},sliceStart=0;
  int line=0,uartFrac=0,elapsed=0,budget=0;
  bool inCPU=false,delayed=false;
};
static Timing loadTiming(CBlockFile &f) {
  Timing t; uint32_t v=0,n=0;
  require(f.FindBlock("Fixture timing")==Result::OKAY,"missing timing block");
  require(f.Read(v)==4 && v==2 && f.Read(n)==4 && n==0x38,"requires v2 timing fixture");
  t.waits.resize(n);
  require(f.Read(t.waits.data(),n)==n && f.Read(t.timerVal)==sizeof(t.timerVal) &&
    f.Read(t.timerOrig)==sizeof(t.timerOrig) && f.Read(t.timerRun)==sizeof(t.timerRun) &&
    f.Read(t.timerStart)==sizeof(t.timerStart) && f.Read(t.irqReq)==4 && f.Read(t.irqEna)==4 &&
    f.Read(t.line)==4 && f.Read(t.uartFrac)==4 && f.Read(t.elapsed)==4 &&
    f.Read(t.sliceStart)==8 && f.Read(t.budget)==4 && f.Read(t.inCPU)==1 && f.Read(t.delayed)==1,
    "truncated timing block");
  require(t.inCPU && !t.delayed && t.budget>0 && t.elapsed>=0 && t.elapsed<t.budget &&
    t.line>=0 && t.line<424 && t.uartFrac>=0 && t.uartFrac<1600,"unsupported slice or callback state");
  for(auto x:t.timerRun) require(!x,"active timers are not yet supported");
  return t;
}
// A fixture is captured by the instruction trace inside an existing Run().
// Starting a new Run() rechecks interrupts, so reject any pending/held line
// rather than assuming that this extra check is harmless. The layout mirrors
// CI960::SaveState in the matching source revision; reject a short read.
static void validateResume(CBlockFile &f) {
  require(f.FindBlock("Fixture i960")==Result::OKAY,"missing i960 state");
  uint8_t prefix[464]{}, irqLines[4]{};int immediate=0, vector=0, priority=0;
  bool stalled=false, burst=false, write=false;int index=0,size=0;uint32_t t1=0,t2=0;
  require(f.Read(prefix,sizeof(prefix))==sizeof(prefix) && f.Read(immediate)==4 && f.Read(vector)==4 &&
    f.Read(priority)==4 && f.Read(stalled)==1 && f.Read(index)==4 && f.Read(size)==4 &&
    f.Read(t1)==4 && f.Read(t2)==4 && f.Read(burst)==1 && f.Read(write)==1 &&
    f.Read(irqLines)==sizeof(irqLines),"truncated i960 resume state");
  require(!immediate && !stalled && !burst && std::all_of(irqLines,irqLines+4,[](uint8_t x){return x==0;}),
    "unsupported pending interrupt or stalled i960 resume");
}
static std::vector<uint8_t> uartBytes(CI8251 &u,const std::string &p) {
  CBlockFile f;require(f.Create(p,"Replay UART","lab")==Result::OKAY,"UART output create failed");
  u.SaveState(&f,"Fixture UART");f.Close();return bytes(p);
}
struct TimedBoard : Board {
  Timing t;
  CI8251 uart;
  int64_t times[2]{},lineEnd=0;
  bool halted[2]{},emptyTrig[2]{},fullTrig[2]{},held=false;
  uint32_t coproCtl=0;
  int running=-1;
  bool arrived=false;
  unsigned lines=0;
  bool commCaptured=false;
  std::vector<uint8_t> commView;
  std::vector<CommEvent> commEvents;
  size_t commIndex=0;
  int64_t now(int which) const {
    if(running!=which) return times[which];
    return times[which] + (which==0 ? int64_t(cpu->CyclesRun())*32 : int64_t(sharc.CyclesRun())*25);
  }
  void unhalt(int which,int64_t when) {
    halted[which]=false;times[which]=std::max(times[which],when);
    if(which==0 && running==1)sharc.EndSlice();
  }
  int areaWaits(uint32_t a) const {
    unsigned o;
    if(a<0x200000)o=0;
    else if(a>=0x500000 && a<0x600000)o=8;
    else if(a>=0x900000 && a<0x980000)o=4;
    else if(a>=0x1800000 && a<0x181c000)o=0x1c;
    else if(a>=0x1a00000 && a<0x1a20000)o=0x20;
    else if(a>=0x1c00000 && a<0x1c80000)o=0x24;
    else if(a>=0x1c80000 && a<0x1d00000)o=0x28;
    else if(a>=0x1d00000 && a<0x1d80000)o=0x2c;
    else if(a>=0x1d80000 && a<0x1e00000)o=0x30;
    else if((a>=0x2000000 && a<0x4000000)||(a>=0x6000000 && a<0x7000000))o=0x34;
    else return 0;
    auto x=t.waits[o];if(x&0x40)return 0;
    switch(x&0x3f){case 2:return 1;case 4:return 2;case 8:return 3;case 16:return 4;default:return 0;}
  }
  bool Burst(UINT32 a) override {
    // Only regions supported by this replay; match CModel2::Burst for them.
    return Bus::Burst(a) || (a>=0x900000 && a<0x980000) ||
      (a>=0x1a00000 && a<0x1a20000 && (a&0xffff)<0x4000) ||
      (a>=0x2000000 && a<0x4000000) || (a>=0x6000000 && a<0x7000000);
  }
  void charge(uint32_t a){cpu->AddWaitCycles(3*areaWaits(a));}
  uint32_t get(uint32_t a,unsigned n) {
    charge(a);
    const auto pc=cpu->GetPIP();
    if(n==4 && a>=0x53c7a0 && a<0x53c7a0+16*0x1e4 &&
       (pc==0xab36c || pc==0xec70 || pc==0xec78 || pc==0xedec))
      throw std::runtime_error("Indy display-prediction state not captured");
    if(a>=0x884000 && a<0x888000) {
      require(n==4,"narrow FIFO read unsupported");
      bool empty=out.empty();uint32_t v=0;
      if(empty) {cpu->Stall();if(!emptyTrig[1]){emptyTrig[1]=true;halted[0]=true;}++cpuStalls;}
      else {v=out.front();out.pop_front();flags();}
      observed.push_back({'R',cpu->GetPIP(),a,0xffffffff,v,unsigned(empty)});return v;
    }
    if(a>=0x900000 && a<0x980000){require((a%4)+n<=4,"unaligned buffer read");return (buffer[(a&0x1ffff)>>2]>>((a&3)*8)) & (n==4?0xffffffffu:((1u<<(n*8))-1));}
    if(a>=0x1a00000 && a<0x1a20000 && (a&0xffff)<0x4000) {
      require(commCaptured && commView.size()==0x4000,"comm view not captured");
      require(n==1||n==2||n==4,"invalid comm access width");
      const unsigned shift=n==4?0:n==2?(a&2)*8:(a&3)*8;
      const uint32_t mask=n==4?0xffffffffu:((1u<<(n*8))-1)<<shift;
      const uint32_t aligned=a&~3u, offset=aligned&0x3fff;
      const uint32_t native=word(commView,offset);
      require(commIndex<commEvents.size(),"unexpected comm read");
      const auto e=commEvents[commIndex++];
      require(e.kind=='R' && e.pc==pc && e.address==aligned && e.mask==mask && e.value==native,"comm read transcript mismatch or asynchronous update");
      return (native&mask)>>shift;
    }
    return Bus::rd(a,n);
  }
  void put(uint32_t a,unsigned n,uint32_t v) {
    charge(a);
    if(a>=0x880000 && a<0x888000) {
      require(n==4,"narrow FIFO write unsupported");
      observed.push_back({'W',cpu->GetPIP(),a,0xffffffff,v,0});
      require(in.size()<capacity,"FIFO input overflow unsupported");
      if(a<0x884000)v=(v&0x800fffff)|((((a&0x3fff)>>4)&0xff)<<23);
      in.push_back(v);
      if(emptyTrig[0]){emptyTrig[0]=false;unhalt(1,now(0));}
      flags();return;
    }
    if(a>=0x900000 && a<0x980000){require((a%4)+n<=4,"unaligned buffer write");unsigned s=(a&3)*8;uint32_t mask=n==4?0xffffffffu:((1u<<(n*8))-1)<<s;auto &x=buffer[(a&0x1ffff)>>2];x=(x&~mask)|((v<<s)&mask);return;}
    Bus::wr(a,n,v);
  }
  UINT8 Read8(UINT32 a) override{return get(a,1);}
  UINT16 Read16(UINT32 a) override{return get(a,2);}
  UINT32 Read32(UINT32 a) override{return get(a,4);}
  void Write8(UINT32 a,UINT8 v) override{put(a,1,v);}
  void Write16(UINT32 a,UINT16 v) override{put(a,2,v);}
  void Write32(UINT32 a,UINT32 v) override{put(a,4,v);}
  UINT32 ReadData(UINT32 a) override {
    if(a>=0x400000 && a<0xc00000) {
      if(in.empty())return 0;
      auto v=in.front();in.pop_front();flags();return v;
    }
    return Board::ReadData(a);
  }
  void WriteData(UINT32 a,UINT32 v) override {
    if(a>=0xc00000 && a<0x1400000) {
      require(out.size()<capacity,"FIFO output overflow unsupported");out.push_back(v);
      if(emptyTrig[1]){emptyTrig[1]=false;unhalt(0,now(1));}flags();return;
    }
    Board::WriteData(a,v);
  }
  void irqUpdate(){cpu->SetIRQLine(0,(t.irqReq&1)!=0);cpu->SetIRQLine(1,(t.irqReq&2)!=0);cpu->SetIRQLine(2,(t.irqReq&0x3fc)!=0);cpu->SetIRQLine(3,(t.irqReq&0xc00)!=0);}
  void soundReady(){if((uart.TxRdy()||uart.RxRdy()) && (t.irqEna&0x400)){t.irqReq|=0x400;irqUpdate();}}
  void endLine() {
    ++lines;t.uartFrac+=32800;unsigned c=t.uartFrac/1600;t.uartFrac%=1600;
    if(c)uart.Clock(c);
    require(t.line!=383 && t.line!=423,"vblank or frame crossing unsupported");
    ++t.line;lineEnd+=32800;
  }
};
static void loadTriggers(CBlockFile &f,TimedBoard &b) {
  require(f.FindBlock("Fixture devices")==Result::OKAY,"missing device block");uint32_t v=0;int64_t a=0,c=0,l=0;
  require(f.Read(v)==4 && v==1 && f.Read(a)==8 && f.Read(c)==8 && f.Read(l)==8 &&
    f.Read(b.halted)==2 && f.Read(b.emptyTrig)==2 && f.Read(b.fullTrig)==2 && f.Read(b.held)==1 && f.Read(b.coproCtl)==4,"truncated flags");
  require(!b.halted[0] && !b.held && !(b.coproCtl&0x80000000) && !b.fullTrig[0] && !b.fullTrig[1],"unsupported initial device state");
  b.times[0]=a;b.times[1]=c;b.lineEnd=l;
}
static bool fullEvents(const std::vector<IO>&a,const std::vector<IO>&b){
  const size_t n=std::min(a.size(),b.size());
  for(size_t i=0;i<n;++i)if(a[i].kind!=b[i].kind||a[i].pc!=b[i].pc||a[i].address!=b[i].address||a[i].mask!=b[i].mask||a[i].value!=b[i].value||a[i].empty!=b[i].empty){
    std::printf("EVENT_MISMATCH index=%zu actual=%c/%08x/%08x/empty%u expected=%c/%08x/%08x/empty%u value_equal=%d\n",i,b[i].kind,b[i].pc,b[i].address,b[i].empty,a[i].kind,a[i].pc,a[i].address,a[i].empty,a[i].value==b[i].value);return false;
  }
  if(a.size()!=b.size()){std::printf("EVENT_COUNT actual=%zu expected=%zu\n",b.size(),a.size());return false;}
  return true;
}
static void firstChanges(const char*name,const std::vector<uint8_t>&a,const std::vector<uint8_t>&b) {
  if(a.size()!=b.size()){std::printf("DIFF %s length=%zu/%zu\n",name,a.size(),b.size());return;}
  unsigned shown=0,total=0;for(size_t i=0;i<a.size();++i)if(a[i]!=b[i]){
    ++total;if(shown++<8)std::printf("DIFF %s offset=%zx\n",name,i);
  }
  if(total)std::printf("DIFF %s changed_bytes=%u\n",name,total);
}
#ifndef INDY_TIMED_SHARC_REPLAY_NO_MAIN
int main(int argc,char **argv) {
  if(argc!=7 && argc!=8){std::fprintf(stderr,"usage: %s fixture.in fixture.out program.bin main_data.bin copro_data.bin fixture.io [--explore-detector-experimental|--explore-detector-native|--explore-response-native]\n",argv[0]);return 2;}
  try {
    const std::string mode=argc==8?argv[7]:"";
    require(mode.empty() || mode=="--explore-detector-experimental" || mode=="--explore-detector-native" || mode=="--explore-response-native","unknown mode");
    const bool exploreDetector=mode=="--explore-detector-experimental" || mode=="--explore-detector-native";
    const bool exploreResponse=mode=="--explore-response-native";
    validateFixtureLayout(argv[1]);validateFixtureLayout(argv[2]);
    auto inComm=fixturePayload(argv[1],"Model 2 comm"),outComm=fixturePayload(argv[2],"Model 2 comm");
    auto inView=fixturePayload(argv[1],"Fixture comm view"),outView=fixturePayload(argv[2],"Fixture comm view");
    require(inComm.empty()==outComm.empty() && inView.empty()==outView.empty(),"comm snapshot missing at one boundary");
    const bool hasComm=!inComm.empty();
    if(hasComm)require(inComm==outComm && inView==outView,"comm state changed between boundaries; asynchronous input or writes unsupported");
    auto rom=bytes(argv[3]),data=bytes(argv[4]),copro=bytes(argv[5]);auto transcript=readIO(argv[6]);
    require(rom.size()==0x200000 && data.size()==0x2000000 && copro.size()==0x800000,"wrong ROM sizes");
    require(word(rom,0xac4e8)==0x90b43400 && word(rom,0xac4fc)==0x90bc3400 && word(rom,0xac504)==0x90ac3400 && word(rom,0xac510)==0x908c3400 && word(rom,0xac2ac)==0x90bd3400 && word(rom,0xac2bc)==0x90b53400 && word(rom,0xac2c4)==0x908d3400 && word(rom,0xac2cc)==0x90953400,"wrong ROM revision");
    char filename[]="/tmp/indy-timed-replay-XXXXXX";int fd=mkstemp(filename);require(fd>=0,"temp failed");close(fd);
    struct Cleanup{std::string p;~Cleanup(){std::remove(p.c_str());}}cleanup{filename};std::string tmp=filename;
    CBlockFile f;require(f.Load(argv[1])==Result::OKAY,"invalid input fixture");
    TimedBoard b;b.meta=metadata(f);b.rom=rom;b.dataRom=data;b.copro=copro;b.experiment=mode!="--explore-detector-native" && !exploreResponse;
    b.commCaptured=hasComm;
    if(hasComm) {
      b.commView.assign(inView.begin()+8,inView.end());
      std::string commPath=argv[6];
      require(commPath.size()>=3 && commPath.substr(commPath.size()-3)==".io","comm fixture requires .io transcript path");
      commPath.replace(commPath.size()-3,3,".comm");b.commEvents=readComm(commPath,word(inView,0)==2);
      for(const auto &e:b.commEvents)require(e.kind=='R',"comm writes unsupported");
      std::printf("COMM_WITNESS version=%u entrypoints=%d (external queues not captured)\n",word(inView,0),word(inView,0)==2);
    }
    if(exploreDetector||exploreResponse)require(b.meta.returnPC==0x20ed8,"exploration requires a response fixture");
    block(f,"Fixture main RAM",b.main);block(f,"Fixture work RAM",b.work);
    require(b.main.size()==0x40000 && b.work.size()==0x100000,"wrong RAM sizes");
    loadDevices(f,b);loadTriggers(f,b);b.t=loadTiming(f);
    require(b.times[0]==b.t.sliceStart+int64_t(b.t.elapsed)*32,"inconsistent slice time");
    require(f.FindBlock("Fixture UART")==Result::OKAY,"missing UART state");b.uart.LoadState(&f,"Fixture UART");
    b.uart.OnRxRdy=[&](bool){b.soundReady();};b.uart.OnTxRdy=[&](bool){b.soundReady();};
    b.uart.OnTransmit=[&](uint8_t){throw std::runtime_error("external UART transmit unsupported");};
    CI960 cpu;b.cpu=&cpu;cpu.AttachBus(&b);cpu.SetFetchRegion(0,b.rom.size(),b.rom.data());cpu.SetFetchRegion(0x200000,b.main.size(),b.main.data());cpu.SetFetchRegion(0x500000,b.work.size(),b.work.data());
    validateResume(f);cpu.LoadState(&f,"Fixture i960");f.Close();
    require(cpu.GetIP()==0xac230 && b.meta.returnPC!=0xac230,"requires pre-selection fixture");
    const bool response=b.meta.returnPC==0x20ed8 && !exploreDetector;
    const uint32_t detectorReturn=cpu.GetReg(CI960::REG_RIP);
    bool peer=false,flag=false,returned=false,physicsRead=false,physicsCall=false,x=false,z=false;
    if(response||exploreDetector)require(detectorReturn && detectorReturn!=b.meta.returnPC && detectorReturn!=0xac230,"invalid detector return");
    const uint32_t stopPC=exploreDetector?detectorReturn:b.meta.returnPC;
    cpu.SetInstructionGate(stopPC,[&](){b.arrived=true;return false;});
    cpu.SetTraceCallback([&](uint32_t ip){
      if(++b.cpuIns>1000000)throw std::runtime_error("i960 instruction limit");
      if(!response && !exploreDetector)return;
      if(ip==0xac264 && cpu.GetReg(16)==0x53c7a0+(1-b.meta.actor)*0x1e4)peer=true;
      if(ip==0xac340 && peer && word(b.work,0x440b0)==1)flag=true;
      if(exploreDetector)return;
      if(ip==detectorReturn) {
        require(peer && flag && word(b.work,0x440b0)==1,"same peer was not selected");returned=true;
      }
      if(returned && ip==0xac230)throw std::runtime_error("detector reentered before response");
      if(returned && ip==0x21dc8) {require(word(b.work,0x440b0)==1,"collision flag changed before physics");physicsRead=true;}
      if(physicsRead && !physicsCall && ip==0x21e3c)throw std::runtime_error("physics response path was skipped");
      if(physicsRead && ip==0x21e28)physicsCall=true;
      if(physicsCall && ip==0x20ec4)x=true;
      if(x && ip==0x20ed0)z=true;
    });
    b.sharc.SetTraceCallback([&](uint32_t){if(++b.sharcIns>2000000)throw std::runtime_error("SHARC instruction limit");});
    // Resume the remainder of the already-running i960 slice, not a fresh quantum.
    int64_t finalSliceStart=b.t.sliceStart;int finalSliceBudget=b.t.budget,finalSliceElapsed=0;
    b.running=0;int used=cpu.Run(b.t.budget-b.t.elapsed);
    b.times[0]+=int64_t(!b.arrived && b.t.elapsed==0 ? std::max(used,1) : used)*32;b.running=-1;
    if(b.arrived)finalSliceElapsed=b.t.elapsed+used;
    for(unsigned guard=0;!b.arrived && guard<200000;++guard){
      bool r0=!b.halted[0] && b.times[0]<b.lineEnd;
      bool r1=!b.halted[1] && b.times[1]<b.lineEnd;
      if(!r0&&!r1){for(int i=0;i<2;++i)if(b.halted[i])b.times[i]=std::max(b.times[i],b.lineEnd);b.endLine();continue;}
      int who=(r0&&(!r1||b.times[0]<=b.times[1]))?0:1,other=1-who,unit=who?25:32;
      int64_t limit=b.lineEnd;if((other?r1:r0))limit=std::min(limit,b.times[other]+2048);
      int cycles=int(std::max<int64_t>(1,(limit-b.times[who]+unit-1)/unit));
      if(who==0){finalSliceStart=b.times[0];finalSliceBudget=cycles;}
      b.running=who;
      int done=who?b.sharc.Run(cycles):cpu.Run(cycles);
      if(b.arrived)finalSliceElapsed=done;
      // At the instruction gate we must retain exact in-slice time, not force
      // the scheduler's minimum slice accounting or advance either CPU.
      b.times[who]+=int64_t(b.arrived?done:std::max(done,1))*unit;b.running=-1;
      require(!cpu.IsFatalHalted() && !b.sharc.IsFatalHalted(),"processor fatal");
    }
    require(b.arrived,"return boundary not reached");
    if(exploreDetector) {
      bool prefix=false;
      if(b.observed.size()<=transcript.size()) {
        std::vector<IO> expectedPrefix(transcript.begin(),transcript.begin()+b.observed.size());
        prefix=fullEvents(expectedPrefix,b.observed);
      }
      uint64_t projectionHash=1469598103934665603ULL;
      for(size_t offset:{size_t(0x440d8),size_t(0x440e0),size_t(0x440e8),size_t(0x440ec)})
        for(int j=0;j<4;++j){projectionHash^=b.work[offset+j];projectionHash*=1099511628211ULL;}
      std::printf("EXPLORATORY_UNVALIDATED mode=%s peer_selected=%d flag=%u g0=%08x substitutions=%u fifo_prefix_equal=%d events=%zu projection_digest=%016llx comm_captured=%d comm_reads=%zu\n",
        b.experiment?"experimental":"native",peer,word(b.work,0x440b0),cpu.GetReg(16),b.substitutions,prefix,b.observed.size(),(unsigned long long)projectionHash,b.commCaptured,b.commIndex);
      std::puts("INCONCLUSIVE: detector branch is not a validated native physics counterfactual");return 1;
    }
    if(response)require(peer && flag && returned && physicsRead && physicsCall && x && z,"peer response path not reproduced");
    if(b.commCaptured)require(b.commIndex==b.commEvents.size(),"unconsumed comm events");
    std::printf("COMM_DIAGNOSTIC captured=%d unchanged=%d events_equal=%d reads=%zu (no comm writes, FG polls, vblank or external updates modeled)\n",
      b.commCaptured,b.commCaptured,b.commCaptured,b.commIndex);
    auto actualCPU=cpuBytes(cpu,tmp),actualSHARC=sharcBytes(b.sharc,tmp),actualUART=uartBytes(b.uart,tmp);
    CBlockFile out;require(out.Load(argv[2])==Result::OKAY,"invalid output fixture");
    std::vector<uint8_t> expectedMain,expectedWork;block(out,"Fixture main RAM",expectedMain);block(out,"Fixture work RAM",expectedWork);
    CI960 ecpu;require(out.FindBlock("Fixture i960")==Result::OKAY,"missing expected CPU");ecpu.LoadState(&out,"Fixture i960");auto expectedCPU=cpuBytes(ecpu,tmp);
    TimedBoard eb;loadDevices(out,eb);loadTriggers(out,eb);eb.t=loadTiming(out);
    require(out.FindBlock("Fixture UART")==Result::OKAY,"missing expected UART");eb.uart.LoadState(&out,"Fixture UART");
    auto expectedSHARC=sharcBytes(eb.sharc,tmp),expectedUART=uartBytes(eb.uart,tmp);out.Close();
    bool events=fullEvents(transcript,b.observed),cpuEqual=actualCPU==expectedCPU,sharcEqual=actualSHARC==expectedSHARC,uartEqual=actualUART==expectedUART;
    bool mem=b.main==expectedMain&&b.work==expectedWork,queues=b.in==eb.in&&b.out==eb.out&&b.buffer==eb.buffer;
    bool timing=b.times[0]==eb.times[0]&&b.times[1]==eb.times[1]&&b.lineEnd==eb.lineEnd&&b.t.line==eb.t.line&&b.t.uartFrac==eb.t.uartFrac&&b.t.irqReq==eb.t.irqReq&&b.t.irqEna==eb.t.irqEna &&
      finalSliceStart==eb.t.sliceStart&&finalSliceBudget==eb.t.budget&&finalSliceElapsed==eb.t.elapsed&&b.t.waits==eb.t.waits &&
      std::equal(b.t.timerVal,b.t.timerVal+4,eb.t.timerVal)&&std::equal(b.t.timerOrig,b.t.timerOrig+4,eb.t.timerOrig)&&
      std::equal(b.t.timerRun,b.t.timerRun+4,eb.t.timerRun)&&std::equal(b.t.timerStart,b.t.timerStart+4,eb.t.timerStart);
    bool flags=b.held==eb.held&&b.coproCtl==eb.coproCtl&&std::equal(b.halted,b.halted+2,eb.halted)&&std::equal(b.emptyTrig,b.emptyTrig+2,eb.emptyTrig)&&std::equal(b.fullTrig,b.fullTrig+2,eb.fullTrig);
    if(exploreResponse) {
      uint32_t ex=word(expectedWork,0x3c59c),ez=word(expectedWork,0x3c5a4),nx=word(b.work,0x3c59c),nz=word(b.work,0x3c5a4);
      float exf,ezf,nxf,nzf;std::memcpy(&exf,&ex,4);std::memcpy(&ezf,&ez,4);std::memcpy(&nxf,&nx,4);std::memcpy(&nzf,&nz,4);
      require(std::isfinite(exf) && std::isfinite(ezf) && std::isfinite(nxf) && std::isfinite(nzf),"non-finite physics position");
      std::printf("EXPLORATORY_UNVALIDATED_RESPONSE native_same_peer=1 physics_x_equal=%d physics_z_equal=%d position_delta=%.6f work_changed_bytes=%zu\n",
        ex==nx,ez==nz,std::hypot(double(nxf)-exf,double(nzf)-ezf),changed(expectedWork,b.work));
    }
    if(!cpuEqual)firstChanges("i960",expectedCPU,actualCPU);
    if(!sharcEqual)firstChanges("SHARC",expectedSHARC,actualSHARC);
    if(!mem){firstChanges("main",expectedMain,b.main);firstChanges("work",expectedWork,b.work);}
    std::printf("BOUNDARY cpu=%d sharc=%d uart=%d memory=%d fifo_buffer=%d events_including_stalls=%d timing=%d flags=%d lines=%u instructions=%u/%u stalls=%u substitutions=%u\n",cpuEqual,sharcEqual,uartEqual,mem,queues,events,timing,flags,b.lines,b.cpuIns,b.sharcIns,b.cpuStalls,b.substitutions);
    std::printf("TIMES actual=%lld/%lld/%lld expected=%lld/%lld/%lld\n",(long long)b.times[0],(long long)b.times[1],(long long)b.lineEnd,(long long)eb.times[0],(long long)eb.times[1],(long long)eb.lineEnd);
    if(response)std::printf("UNVALIDATED_REPLAY_PATH selected_peer=1 flag=1 detector_return=1 physics_read=1 physics_call=1 x_store=1 z_store=1\n");
    std::puts("INCONCLUSIVE: diagnostic subset only; full board and external device state not validated");return 1;
  } catch(const std::exception &e) {std::fprintf(stderr,"INCONCLUSIVE: %s\n",e.what());return 1;}
}
#endif
