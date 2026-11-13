#include "ScudPacket.h"
#include "Srally2Packet.h"
#include "Lemans24Packet.h"
#include "Network/Games/Policy.h"
#include "Network/Games/Daytona2/Daytona2StartSync.h"
// ROM-free tests of the real HLE board and TCP transport. GPL-3.0-or-later.
#include "Supermodel.h"
#include "Util/NewConfig.h"
#include "Network/NetServer.h"
#include "Model3/INetBoard.h"
#include "OSD/Thread.h"
#include "Daytona2Packet.h"
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>
// Test-only inspection/fault injection; no test hooks in the production API.
#define private public
#include "Model3/SimNetBoard.h"
#undef private
#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <iostream>

using Clock = std::chrono::steady_clock;
static std::atomic<unsigned> errors{0};
Result ErrorLog(const char* fmt, ...)
{
  ++errors;
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fputc('\n', stderr);
  return Result::FAIL;
}
void InfoLog(const char*, ...) {}
void DebugLog(const char*, ...) {}
static void SleepForMs(unsigned ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

struct Cabinet
{
  Util::Config::Node config{"test"};
  std::vector<uint8_t> ram = std::vector<uint8_t>(0x10000);
  std::vector<uint8_t> buffer = std::vector<uint8_t>(0x20000);
  std::shared_ptr<CNetServer> server;
  std::unique_ptr<CSimNetBoard> board;

  Cabinet(unsigned in, unsigned out, const std::string& game, unsigned role, unsigned segment, bool legacySettings = false, bool async = true, bool deferredListen = false, const std::string& relayEndpoint = "", bool rallyEight=false)
  {
    config.Set("Network", true);
    config.Set("NetSrally2Eight",rallyEight);
    config.Set("NetRelay", relayEndpoint);
    config.Set("AsyncNet", async);
    // Obsolete INI entries must not override the new automatic policy.
    if (legacySettings) {
      config.Set("Daytona2PositionPrediction", false);
      config.Set("Daytona2SyncStart", false);
    }
    config.Set("PortIn", in);
    config.Set("PortOut", out);
    config.Set("AddressOut", std::string("127.0.0.1"));
    server = std::make_shared<CNetServer>(in, "Async HLE regression test", relayEndpoint.empty());
    if (!deferredListen) assert(server->Start());
    else assert(!server->Connect("127.0.0.1", in)); // not running before board Init
    board = std::make_unique<CSimNetBoard>(config, server);
    Game info;
    info.name = game;
    if (game == "dayto2pe") info.parent = "daytona2";
    if (game == "scudplus") info.parent = "scud";
    info.netboard_present = true;
    board->GetGame(info);
    assert(board->Init(ram.data(), buffer.data()) == Result::OKAY);
    assert(board->m_gameNet.daytona.m_daytoPrediction == (Dayto2pe::AsyncEnabled(game,async)));
    assert(board->m_gameNet.m_syncStart == (async && GameNetwork::IsSupportedGame(game)));
    assert(board->m_gameNet.ImmediateSelfEcho() == !(async && Srally2::PreserveRingSelfEcho(game)));
    if (!relayEndpoint.empty()) board->SetRelayNativeReady(); // EEPROM is tested separately
    const unsigned base = board->m_gameType == GameType::one ? 0x400 : 0x200;
    uint16_t value = role;
    memcpy(ram.data() + base, &value, 2);
    value = segment;
    memcpy(ram.data() + base + 4, &value, 2);
  }

  void Start()
  {
    board->WriteIORegister(0xc0, 1);
    board->RunFrame();
    board->WriteIORegister(0x88, 0xf000);
    board->RunFrame();
    assert(board->m_state == State::testing);
  }
};

int main(int argc, char** argv)
{
  setvbuf(stdout, nullptr, _IONBF, 0);
  assert(argc >= 6);
  const std::string scenario = argv[1], game = argv[2];
  const unsigned n = std::stoul(argv[3]);
  assert(unsigned(argc) == 4 + 2 * n);
  const bool relayCase = scenario == "start-relay" || scenario == "predict-relay-cpu" || scenario == "scud-relay-predict" || scenario == "srally2-relay-predict" || scenario == "lemans24-relay-predict" || scenario == "stream-relay-smoothness";
  const bool cpuCase = scenario == "predict-cpu" || scenario == "predict-relay-cpu";
  const bool startCase = scenario.rfind("start", 0) == 0;
  const bool poseCase = scenario.rfind("predict", 0) == 0;
  const unsigned segment = Scud::IsSupportedGame(game) ? 1024 : Srally2::IsSupportedGame(game) ? 2048 : Lemans24::IsSupportedGame(game) ? 0x0CC0 : startCase ? 848 : poseCase ? (scenario == "predict-size" ? 844 : 848) :
    scenario == "zero" ? 0 : scenario == "oversize" ? 0xffff : Dayto2pe::IsSupportedGame(game) ? 848 : 64;
  std::vector<std::unique_ptr<Cabinet>> c;
  // Make every server/link before starting to advance the handshake.
  for (unsigned i = 0; i < n; i++)
  {
    unsigned role = i;
    if (scenario == "relay" && i == n - 1) role = 0x8000;
    c.emplace_back(new Cabinet(std::stoul(argv[4 + i]), std::stoul(argv[4 + n + i]), (scenario == "start-mixed-daytona" && i == 1) ? "dayto2pe" : game, role, segment, scenario == "predict-legacy-settings" || scenario == "start-legacy-settings", scenario != "start-async-off", scenario == "startup-deferred", relayCase ? "127.0.0.1:" + std::string(argv[4+n+i]) : "",game=="srally2"&&n>4));
  }
  // Emulate an older peer without entry sync, not a removed user option.
  if (scenario == "start-mismatch") c[1]->board->m_gameNet.m_syncStart = false;
  for (auto& cab : c) cab->Start();
  double maxFrameMs = 0;
  bool oldGateInjected = false;
  auto tick = [&] {
    for (auto& cab : c)
    {
      if ((scenario == "start-old-gate" || scenario == "start-old-cohort") && !oldGateInjected)
      {
        std::lock_guard<std::mutex> lock(cab->board->m_link->m_mutex);
        for (auto& queued : cab->board->m_link->m_queue)
        {
          auto& packet = queued.data;
          if (packet.size() == sizeof(uint64_t))
          {
            uint64_t guid; memcpy(&guid,packet.data(),sizeof(guid));
            if (guid == cab->board->m_gameNet.AsyncProtocolGUID(0))
            {
              guid = scenario=="start-old-cohort" ? guid ^ 0x0000000300000000ULL : 0x5bf177da34874;
              memcpy(packet.data(),&guid,sizeof(guid)); oldGateInjected = true; break;
            }
          }
        }
      }
      const auto before = Clock::now();
      if (relayCase && cab->board->RelayCabinetIndex() >= 0) {
        const unsigned base = cab->board->m_gameType == GameType::one ? 0x400 : 0x200;
        uint16_t role = Srally2::IsSupportedGame(game)
            ? uint16_t(cab->board->RelayCabinetIndex())
            : uint16_t(cab->board->RelayCabinetIndex() == 0 ? 0 : 1);
        memcpy(cab->ram.data()+base,&role,2);
      }
      cab->board->RunFrame();
      maxFrameMs = std::max(maxFrameMs, std::chrono::duration<double, std::milli>(Clock::now() - before).count());
    }
    SleepForMs(2);
  };
  auto allReady = [&] {
    return std::all_of(c.begin(), c.end(), [](const auto& cab) { return cab->board->m_state == State::ready; });
  };
  auto anyError = [&] {
    return std::any_of(c.begin(), c.end(), [](const auto& cab) { return cab->board->m_state == State::error; });
  };
  if (scenario == "mismatch" || scenario == "mismatch-master")
  {
    assert(n == 2);
    auto& legacyBoard = *c[scenario == "mismatch" ? 1 : 0]->board;
    auto& asyncBoard = *c[scenario == "mismatch" ? 0 : 1]->board;
    legacyBoard.m_async = false;
    // Legacy setup is blocking: run that cabinet on its own thread.
    std::thread legacy([&] {
      while (legacyBoard.m_state == State::testing)
      {
        legacyBoard.RunFrame();
        SleepForMs(2);
      }
    });
    const auto deadline = Clock::now() + std::chrono::seconds(15);
    while (asyncBoard.m_state != State::error && Clock::now() < deadline)
    {
      asyncBoard.RunFrame();
      SleepForMs(2);
    }
    legacy.join();
    assert(asyncBoard.m_state == State::error);
    assert(legacyBoard.m_state == State::error);
    std::cout << "PASS " << scenario << " rejected\n";
    return 0;
  }
  if (scenario == "sync")
  {
    assert(n == 1); // blocking loopback exercises the unchanged legacy path
    c[0]->board->m_async = false;
  }
  const auto deadline = Clock::now() + std::chrono::seconds(15);
  while (!allReady() && !anyError() && Clock::now() < deadline) tick();
  if (scenario == "zero" || scenario == "oversize" || scenario == "predict-size")
  {
    assert(anyError());
    std::cout << "PASS invalid segment bounds rejected\n";
    return 0;
  }
  if (scenario == "start-mismatch" || scenario == "start-old-gate" || scenario == "start-old-cohort" || scenario == "start-mixed-daytona")
  {
    assert(anyError());
    if (scenario == "start-old-gate" || scenario == "start-old-cohort") assert(oldGateInjected);
    std::cout << "PASS start mode mismatch rejected before ready\n";
    return 0;
  }
  assert(allReady());
  if (relayCase) std::sort(c.begin(),c.end(),[](const auto& a,const auto& b) {
    return a->board->m_asyncIndex[0] < b->board->m_asyncIndex[0];
  });
  if (scenario == "srally2-ring-echo")
  {
    assert(Srally2::PreserveRingSelfEcho(game) && segment == 2048 && n >= 2);
    auto writeLocal = [&](unsigned cabIdx, uint8_t phase, uint8_t ack) {
      for (unsigned bank : {0u, 0x10000u})
      {
        auto* tx = c[cabIdx]->buffer.data() + bank + 0x100;
        std::fill(tx, tx + segment, uint8_t(0x10 + cabIdx));
        tx[0x44] = phase;
        tx[0x6c] = ack;
      }
    };
    for (unsigned i = 0; i < n; ++i) writeLocal(i, 0, 0);

    // Wait for initial non-zero ring fill across all slots, including self-echo.
    const auto settleDeadline = Clock::now() + std::chrono::seconds(10);
    bool settled = false;
    while (!settled && Clock::now() < settleDeadline)
    {
      tick();
      settled = true;
      for (unsigned i = 0; i < n; ++i)
        for (unsigned slot = 1; slot <= n; ++slot)
          if (c[i]->board->ReadCommRAM8(0x100 + slot * segment) != uint8_t(0x10 + (i + n - slot) % n))
            settled = false;
    }
    assert(settled);

    // Prove local CommRAM edits while a round is in flight do NOT leak into
    // slot n (self-echo) before the ring round returns.
    while (c[0]->board->m_asyncInFlight && Clock::now() < settleDeadline) tick();
    assert(!c[0]->board->m_asyncInFlight);
    writeLocal(0, 0x77, 0x33);
    c[0]->board->RunFrame(); // launches round carrying (0x77, 0x33)
    assert(c[0]->board->m_asyncInFlight);
    // Immediately clear local TX on the next local frame while round is in flight.
    writeLocal(0, 0x00, 0x00);
    c[0]->board->RunFrame();
    assert(c[0]->board->ReadCommRAM8(0x100 + n * segment + 0x6c) == 0);

    bool peerSawPulse = false, selfSawPulse = false;
    while ((!peerSawPulse || !selfSawPulse) && Clock::now() < settleDeadline)
    {
      tick();
      if (c[1]->board->ReadCommRAM8(0x100 + 1 * segment + 0x6c) == 0x33) peerSawPulse = true;
      if (c[0]->board->ReadCommRAM8(0x100 + n * segment + 0x6c) == 0x33) selfSawPulse = true;
    }
    assert(peerSawPulse && selfSawPulse);

    // Settle back to (0, 0) before running the srally2 0xFF84E1DC 1->2->3->0 barrier.
    bool cleared = false;
    while (!cleared && Clock::now() < settleDeadline)
    {
      tick();
      cleared = true;
      for (unsigned i = 0; i < n; ++i)
        for (unsigned slot = 1; slot <= n; ++slot)
          if (c[i]->board->ReadCommRAM8(0x100 + slot * segment + 0x6c) != 0 ||
              c[i]->board->ReadCommRAM8(0x100 + slot * segment + 0x44) != 0)
            cleared = false;
    }
    assert(cleared);

    // Model srally2's 0xFF84E1DC menu/track selection barrier: coordinator (cab 0)
    // advances phase 1 -> 2 -> 3 once all slots 1..n report ack == phase, and
    // EVERY cabinet clears (phase, ack) -> (0, 0) on the very first frame it
    // sees ack == 3 across all slots 1..n (including its own self-echo slot n).
    uint8_t coordPhase = 1;
    std::vector<uint8_t> localAck(n, 0);
    std::vector<bool> exited(n, false);
    writeLocal(0, coordPhase, 0);
    const auto barrierDeadline = Clock::now() + std::chrono::seconds(12);
    while (!std::all_of(exited.begin(), exited.end(), [](bool v) { return v; }) &&
           Clock::now() < barrierDeadline)
    {
      tick();
      for (unsigned i = 0; i < n; ++i)
      {
        if (exited[i]) continue;
        auto& b = *c[i]->board;
        // Coordinator slot in CommRAM for cabinet i is slot ((i == 0) ? n : i).
        const unsigned coordSlot = (i == 0) ? n : i;
        const uint8_t seenCoordPhase = (i == 0) ? coordPhase : b.ReadCommRAM8(0x100 + coordSlot * segment + 0x44);
        if (seenCoordPhase > localAck[i] && localAck[i] < 3)
        {
          localAck[i] = uint8_t(localAck[i] + 1);
          writeLocal(i, (i == 0) ? coordPhase : 0, localAck[i]);
        }
        auto allSlotsAck = [&](uint8_t target) {
          for (unsigned slot = 1; slot <= n; ++slot)
            if (b.ReadCommRAM8(0x100 + slot * segment + 0x6c) != target)
              return false;
          return true;
        };
        if (i == 0 && coordPhase < 3 && allSlotsAck(coordPhase))
        {
          ++coordPhase;
          writeLocal(0, coordPhase, localAck[0]);
        }
        if (localAck[i] == 3 && allSlotsAck(3))
        {
          // Immediate clear upon barrier completion (0xFF84E0A0).
          exited[i] = true;
          coordPhase = (i == 0) ? 0 : coordPhase;
          writeLocal(i, 0, 0);
        }
      }
    }
    assert(std::all_of(exited.begin(), exited.end(), [](bool v) { return v; }));
    assert(errors == 0);
    std::cout << "PASS " << scenario << ' ' << game << " cabinets=" << n
              << " round-consistent self-echo preserved barrier ack=3 across all peers\n";
    return 0;
  }
  if (scenario == "stream-smoothness" || scenario == "stream-relay-smoothness")
  {
    assert(n >= 2 && GameNetwork::IsSupportedGame(game));
    if (Scud::IsSupportedGame(game))
      for (unsigned i = 0; i < n; ++i) c[i]->board->SetScudRouting(ScudRoute(i + 1, n));
    constexpr double stepX = 1.2;
    const auto paceStart = Clock::now();
    unsigned uniqueArrivals = 0, stalledFrames = 0, measuredFrames = 0;
    uint32_t lastSeenTick = 0;
    double prevX = 0.0, minStep = 1e9, maxStep = 0.0;
    for (unsigned frame = 1; frame <= 240; ++frame)
    {
      for (unsigned i = 0; i < n; ++i)
      {
        const float x = float(i * 100.0 + frame * stepX);
        if (Dayto2pe::IsSupportedGame(game))
        {
          auto pkt = MakePacket(uint16_t(frame), x, float(-int(i) * 20),
                                game == "daytona2" ? Dayto2pe::BattleOnTheEdge : Dayto2pe::PowerEdition);
          for (unsigned bank : {0u, 0x10000u})
            memcpy(c[i]->buffer.data() + bank + 0x100, pkt.data(), segment);
        }
        else if (Scud::IsSupportedGame(game))
        {
          auto pkt = ScudPacket(i + 1, frame, n);
          Scud::WriteFloat(pkt.data(), 0x60, x);
          for (unsigned bank : {0u, 0x10000u})
            memcpy(c[i]->buffer.data() + bank + 0x100, pkt.data(), segment);
        }
        else if (Lemans24::IsSupportedGame(game))
        {
          auto pkt = MakeLemans24RacePacket(i, n, frame, x, 5.0f, 10.0f, float(stepX), 0.0f, 0.0f, 0, 1024, 0);
          for (unsigned bank : {0u, 0x10000u})
            memcpy(c[i]->buffer.data() + bank + 0x100, pkt.data(), segment);
        }
        else
        {
          std::vector<uint8_t> pkt(segment, uint8_t(0x20 + i));
          Dayto2pe::WriteFloat(pkt.data(), 0x80, x);
          pkt[0x3a] = uint8_t(frame >> 8);
          pkt[0x3b] = uint8_t(frame & 0xff);
          for (unsigned bank : {0u, 0x10000u})
            memcpy(c[i]->buffer.data() + bank + 0x100, pkt.data(), segment);
        }
        c[i]->board->RunFrame();
        assert(c[i]->board->m_state == State::ready);
      }
      // Measure remote cabinet 0 as seen by cabinet 1 (slot 0 -> CommRAM + 0x100 + segment).
      std::vector<uint8_t> visible(segment);
      for (unsigned byte = 0; byte < segment; ++byte)
        visible[byte] = c[1]->board->ReadCommRAM8(0x100 + segment + byte);
      const auto* raw = c[1]->board->m_asyncLatest.data();
      uint32_t seenTick = 0;
      double curX = 0.0;
      if (Dayto2pe::IsSupportedGame(game))
      {
        seenTick = Dayto2pe::Read16(raw, 0x3a);
        curX = Dayto2pe::ReadFloat(visible.data(), 0x6c);
      }
      else if (Scud::IsSupportedGame(game))
      {
        seenTick = Scud::Read32(raw, 0x38);
        curX = Scud::ReadFloat(visible.data(), 0x60);
      }
      else if (Lemans24::IsSupportedGame(game))
      {
        seenTick = Lemans24::Read8(raw, 0x850) & 0x7Fu;
        curX = Lemans24::ReadFloat(visible.data(), Lemans24::HumanPoseOffset + 0x08);
      }
      else
      {
        seenTick = (uint32_t(raw[0x3a]) << 8) | raw[0x3b];
        curX = Dayto2pe::ReadFloat(visible.data(), 0x80);
      }
      if (frame == 80)
      {
        lastSeenTick = seenTick;
        prevX = curX;
      }
      else if (frame > 80)
      {
        ++measuredFrames;
        if (seenTick != lastSeenTick)
        {
          ++uniqueArrivals;
          lastSeenTick = seenTick;
        }
        const double dx = curX - prevX;
        prevX = curX;
        minStep = std::min(minStep, dx);
        maxStep = std::max(maxStep, dx);
        if (dx < 0.25 * stepX)
          ++stalledFrames;
      }
      std::this_thread::sleep_until(paceStart + std::chrono::microseconds(uint64_t(frame) * 16667ULL));
    }
    std::cout << "PASS " << scenario << ' ' << game << " cabinets=" << n
              << " unique_packets=" << uniqueArrivals << "/" << measuredFrames
              << " stalled_frames=" << stalledFrames
              << " rel_step=[" << (minStep / stepX) << "," << (maxStep / stepX) << "]\n";
    // Under 50 ms one-way latency (100 ms RTT), stop-and-wait only delivered
    // ~22/160 packets (8.5 Hz) and stalled ~138/160 frames on srally2.
    assert(uniqueArrivals >= 130);
    assert(stalledFrames <= 10);
    assert(errors == 0);
    return 0;
  }
  if (scenario == "scud-predict" || scenario == "scud-relay-predict")
  {
    assert(Scud::IsSupportedGame(game));
    std::vector<std::vector<bool>> humanByCabSlot(n, std::vector<bool>(n - 1, false));
    std::vector<bool> cpuByFollower(n, false);
    for(unsigned i=0;i<n;++i)c[i]->board->SetScudRouting(ScudRoute(i+1,n,n,1));
    for(unsigned frame=1;frame<=600;++frame) {
      for(unsigned i=0;i<n;++i) { auto p=ScudPacket(i+1,frame,n,1);for(unsigned bank:{0u,0x10000u})memcpy(c[i]->buffer.data()+bank+0x100,p.data(),p.size()); }
      tick();
      for(unsigned i=0;i<n;++i) {
        auto& b=*c[i]->board;assert(b.m_state==State::ready);const auto local=ScudPacket(i+1,frame,n,1);
        for(unsigned byte=0;byte<segment;++byte)assert(b.ReadCommRAM8(0x100+n*segment+byte)==local[byte]);
        for(unsigned slot=0;slot<n;++slot) {
          const auto raw=b.m_asyncLatest.data()+slot*segment;uint32_t stamp;
          if(!Scud::DecodeHeader(raw,segment,stamp))continue;
          const unsigned owner=1+(i+n-1-slot)%n;const auto original=ScudPacket(owner,stamp,n,1);
          assert(!memcmp(raw,original.data(),segment));if(slot==n-1)continue;
          for(unsigned byte=0;byte<segment;++byte) {
            const unsigned off=byte^2;const bool h=(off>=0x60&&off<0x64)||(off>=0x68&&off<0x6c);
            const bool cpuByte=owner==1 && i!=0 && off>=0x124+4*n && off<0x1c4 && (off-0x124)%4>=2;
            const auto visible=b.ReadCommRAM8(0x100+(slot+1)*segment+byte);
            if(!h&&!cpuByte)assert(visible==raw[byte]);
            else if(visible!=raw[byte]) {
              if(h) humanByCabSlot[i][slot]=true;
              if(cpuByte) cpuByFollower[i]=true;
            }
          }
        }
      }
    }
    for(unsigned i=0;i<n;++i) {
      for(unsigned slot=0;slot+1<n;++slot)
        assert(humanByCabSlot[i][slot]);
      if(i!=0) assert(cpuByFollower[i]);
    }
    assert(errors==0);c[0]->board->Reset();assert(c[0]->board->m_gameNet.scud.predictors.empty());
    std::cout<<"PASS "<<scenario<<' '<<game<<" cabinets="<<n<<" all-to-all human & CPU progress changed; raw forwarding, local echo, flags, headers and non-authority CPUs unchanged\n";return 0;
  }
  if (scenario == "srally2-predict" || scenario == "srally2-relay-predict")
  {
    assert(Srally2::IsSupportedGame(game));
    std::vector<std::vector<bool>> humanByCabSlot(n, std::vector<bool>(n - 1, false));
    std::vector<bool> cpuByFollower(n, false);
    for (unsigned frame = 1; frame <= 600; ++frame) {
      for (unsigned i = 0; i < n; ++i) {
        const float x = float(i * 40.0 + 0.85 * double(frame));
        const float y = float(5.0 + 0.04 * double(frame));
        const float z = float(i * 15.0 + 0.45 * double(frame));
        const int16_t yaw = int16_t(1000 + int(frame * 24));
        auto p = MakeSrally2RacePacket(i, frame, x, y, z, 0.85f, 0.04f, 0.45f,
                                       64, yaw, -32, 0.0023f, i == 0 ? 3u : 0u);
        for (unsigned bank : {0u, 0x10000u})
          memcpy(c[i]->buffer.data() + bank + 0x100, p.data(), p.size());
      }
      tick();
      for (unsigned i = 0; i < n; ++i) {
        auto& b = *c[i]->board;
        assert(b.m_state == State::ready);
        for (unsigned slot = 0; slot < n; ++slot) {
          const auto* raw = b.m_asyncLatest.data() + slot * segment;
          const uint32_t mode = Srally2::Read32(raw, 0x08);
          if (mode != 1u) continue;
          const uint32_t stamp = Srally2::Read32(raw, 0x74);
          const unsigned remoteCab = (i + n - 1 - slot) % n;
          const float x = float(remoteCab * 40.0 + 0.85 * double(stamp));
          const float y = float(5.0 + 0.04 * double(stamp));
          const float z = float(remoteCab * 15.0 + 0.45 * double(stamp));
          const int16_t yaw = int16_t(1000 + int(stamp * 24));
          const auto original = MakeSrally2RacePacket(remoteCab, stamp, x, y, z, 0.85f, 0.04f, 0.45f,
                                                      64, yaw, -32, 0.0023f, remoteCab == 0 ? 3u : 0u);
          assert(!memcmp(raw, original.data(), segment));
          // Self-echo slot (n - 1) must stay round-consistent and never be predicted.
          if (slot == n - 1) {
            for (unsigned byte = 0; byte < segment; ++byte)
              assert(b.ReadCommRAM8(0x100 + (slot + 1) * segment + byte) == raw[byte]);
            continue;
          }
          const unsigned numCpus = remoteCab == 0 ? 3u : 0u;
          for (unsigned byte = 0; byte < segment; ++byte) {
            const unsigned off = byte ^ 2;
            const auto inPoseEditable = [](unsigned rel) {
              return (rel < 0x0C) || (rel >= 0x18 && rel < 0x1E);
            };
            const bool h = off >= Srally2::HumanPoseOffset &&
                           off < Srally2::HumanPoseOffset + Srally2::PoseStride &&
                           inPoseEditable(off - Srally2::HumanPoseOffset);
            bool cpuEdit = false;
            for (unsigned ci = 0; ci < numCpus; ++ci) {
              const unsigned base = unsigned(Srally2::CpuPoseBase + ci * Srally2::PoseStride);
              if (off >= base && off < base + Srally2::PoseStride && inPoseEditable(off - base))
                cpuEdit = true;
            }
            const auto visible = b.ReadCommRAM8(0x100 + (slot + 1) * segment + byte);
            if (!h && !cpuEdit) assert(visible == raw[byte]);
            else if (visible != raw[byte]) {
              if (h) humanByCabSlot[i][slot] = true;
              if (cpuEdit) cpuByFollower[i] = true;
            }
          }
        }
      }
    }
    for (unsigned i = 0; i < n; ++i) {
      for (unsigned slot = 0; slot + 1 < n; ++slot)
        assert(humanByCabSlot[i][slot]);
      if (i != 0) assert(cpuByFollower[i]);
    }
    assert(errors == 0);
    c[0]->board->Reset();
    assert(c[0]->board->m_gameNet.srally2.predictors.empty());
    std::cout << "PASS " << scenario << ' ' << game << " cabinets=" << n
              << " all-to-all 3D human & CPU poses predicted; barriers, self-echo, velocities and flags untouched\n";
    return 0;
  }
  if (scenario == "lemans24-predict" || scenario == "lemans24-relay-predict")
  {
    assert(Lemans24::IsSupportedGame(game) && n >= 2 && n <= Lemans24::MaxCabinets);
    const unsigned numCpus = Lemans24::CpuCountForCabinets(n);
    std::vector<std::vector<bool>> humanByCabSlot(n, std::vector<bool>(n - 1, false));
    std::vector<std::vector<bool>> cpuByCabSlot(n, std::vector<bool>(n - 1, false));
    for (unsigned frame = 1; frame <= 500; ++frame) {
      for (unsigned i = 0; i < n; ++i) {
        const float x = float(i * 120.0 + 4.5 * double(frame));
        const float y = float(10.0 + 0.15 * double(frame));
        const float z = float(i * 30.0 + 1.5 * double(frame));
        const int16_t yaw = int16_t(2000 + int(frame * 36));
        auto p = MakeLemans24RacePacket(i, n, frame, x, y, z, 4.5f, 0.15f, 1.5f, 48, yaw, -24);
        for (unsigned bank : {0u, 0x10000u})
          memcpy(c[i]->buffer.data() + bank + 0x100, p.data(), p.size());
      }
      tick();
      for (unsigned i = 0; i < n; ++i) {
        auto& b = *c[i]->board;
        assert(b.m_state == State::ready);
        const auto local = MakeLemans24RacePacket(
            i, n, frame,
            float(i * 120.0 + 4.5 * double(frame)),
            float(10.0 + 0.15 * double(frame)),
            float(i * 30.0 + 1.5 * double(frame)),
            4.5f, 0.15f, 1.5f, 48, int16_t(2000 + int(frame * 36)), -24);
        for (unsigned byte = 0; byte < segment; ++byte)
          assert(b.ReadCommRAM8(0x100 + n * segment + byte) == local[byte]);
        for (unsigned slot = 0; slot + 1 < n; ++slot) {
          const auto* raw = b.m_asyncLatest.data() + slot * segment;
          if (Lemans24::Read32(raw, 0x08) != 1u) continue;
          const unsigned remoteCab = (i + n - 1 - slot) % n;
          assert(Lemans24::Read32(raw, 0x28) == remoteCab);
          for (unsigned byte = 0; byte < segment; ++byte) {
            const unsigned off = byte ^ 2;
            const auto inCarEdit = [](unsigned rel) { return rel >= 0x02 && rel < 0x14; };
            const bool h = off >= Lemans24::HumanPoseOffset &&
                           off < Lemans24::HumanPoseOffset + Lemans24::PoseStride &&
                           inCarEdit(off - unsigned(Lemans24::HumanPoseOffset));
            bool cpuEdit = false;
            for (unsigned ci = 0; ci < numCpus; ++ci) {
              const unsigned base = unsigned(Lemans24::CpuPoseBase + ci * Lemans24::PoseStride);
              if (off >= base && off < base + Lemans24::PoseStride && inCarEdit(off - base))
                cpuEdit = true;
            }
            const auto visible = b.ReadCommRAM8(0x100 + (slot + 1) * segment + byte);
            if (!h && !cpuEdit) assert(visible == raw[byte]);
            else if (visible != raw[byte]) {
              if (h) humanByCabSlot[i][slot] = true;
              if (cpuEdit) cpuByCabSlot[i][slot] = true;
            }
          }
        }
      }
    }
    for (unsigned i = 0; i < n; ++i) {
      for (unsigned slot = 0; slot + 1 < n; ++slot) {
        assert(humanByCabSlot[i][slot]);
        assert(cpuByCabSlot[i][slot]);
      }
    }
    assert(errors == 0);
    c[0]->board->Reset();
    assert(c[0]->board->m_gameNet.lemans24.predictors.empty());
    std::cout << "PASS " << scenario << ' ' << game << " cabinets=" << n
              << " all-to-all 3D human & " << numCpus << "-CPU poses predicted; Step 1.5 o^2 headers/wheels/self-echo untouched\n";
    return 0;
  }
  if (startCase)
  {
    for(auto& cab:c) cab->board->SetRaceParticipants((1u<<n)-1);
    assert(Dayto2pe::AtStartGate(15,15,5,2));
    assert(!Dayto2pe::AtStartGate(15,15,5,1));
    assert(!Dayto2pe::AtStartGate(15,15,4,2));
    assert(!Dayto2pe::AtStartGate(15,16,5,2));
    // Full countdown including every possible initial countdown value must
    // remain asynchronous; most importantly, the former GO gate is gone.
    for (uint32_t t=0;t<=627;++t) assert(!Dayto2pe::AtStartGate(15,15,6,t));
    assert(!Dayto2pe::AtStartGate(15,16,6,0xffffffff));
    assert(!Dayto2pe::AtStartGate(16,17,6,0xffffffff));
    int64_t translated;
    assert(Dayto2pe::TranslateStartDeadline(502000000,490000000,10000000,translated) && translated==12000000);
    assert(Dayto2pe::TranslateStartDeadline(3000000,-999000000,1000000000,translated) && translated==1002000000);
    assert(!Dayto2pe::TranslateStartDeadline(INT64_MAX,INT64_MIN,1,translated));
    assert(!Dayto2pe::TranslateStartDeadline(0,0,10000000,translated));
    const bool enabled = scenario != "start-async-off" && GameNetwork::IsSupportedGame(game) && n > 1;
    for (auto& cab : c)
      assert(cab->board->m_gameNet.m_syncStart == (scenario != "start-async-off" && GameNetwork::IsSupportedGame(game)));
    if (!enabled)
    {
      for (auto& cab : c) assert(!cab->board->HoldForRaceStart(true));
      std::cout << "PASS " << scenario << " game=" << game << " n=" << n << " bypasses barrier\n";
      return 0;
    }
    if (scenario == "start-timeout")
    {
      auto& b = *c[0]->board;
      assert(b.HoldForRaceStart(true));
      b.m_gameNet.m_startWaitTime -= 61000000;
      assert(b.HoldForRaceStart(true));
      assert(b.m_gameNet.m_startFailed && b.m_state == State::error);
      assert(b.HoldForRaceStart(true));
      b.Reset(); assert(!b.m_gameNet.m_startFailed && !b.m_gameNet.m_startWaiting);
      std::cout << "PASS barrier timeout holds locally, never falls back to unsynchronized GO\n";
      return 0;
    }
    if (scenario == "start-checkpoint")
    {
      auto& b = *c[0]->board;
      assert(b.HoldForRaceStart(true));
      assert(b.HoldForRaceStart(false));
      assert(b.m_gameNet.m_startFailed && b.m_state == State::error);
      std::cout << "PASS changed entry checkpoint aborts instead of starting\n";
      return 0;
    }
    if (scenario == "start-disconnect")
    {
      auto& b = *c[0]->board;
      assert(b.HoldForRaceStart(true));
      c.back()->board->m_link->m_output->Close();
      const auto stop = Clock::now()+std::chrono::seconds(3);
      while (!b.m_gameNet.m_startFailed && Clock::now()<stop) { b.HoldForRaceStart(true); SleepForMs(2); }
      assert(b.m_gameNet.m_startFailed && b.HoldForRaceStart(true));
      std::cout << "PASS disconnected peer holds barrier instead of starting\n";
      return 0;
    }
    if (scenario == "start-malformed")
    {
      auto& b = *c[0]->board;
      assert(b.HoldForRaceStart(true));
      Dayto2pe::StartToken bad; bad.magic = 0;
      c.back()->board->m_link->Send(&bad,sizeof(bad));
      const auto stop = Clock::now()+std::chrono::seconds(3);
      while (!b.m_gameNet.m_startFailed && Clock::now()<stop) { b.HoldForRaceStart(true); SleepForMs(2); }
      assert(b.m_gameNet.m_startFailed && b.HoldForRaceStart(true));
      std::cout << "PASS malformed barrier token rejected without release\n";
      return 0;
    }
    if(scenario=="start-no-roster") {
      c[0]->board->SetRaceParticipants(0);
      assert(c[0]->board->HoldForRaceStart(true));
      assert(c[0]->board->m_state==State::error && !c[0]->board->m_gameNet.m_startReleased);
      std::cout<<"PASS unknown native roster fails closed without assuming all or ready-now members\n";return 0;
    }
    if(scenario=="start-split") {
      assert(n==5);const uint32_t masks[]={3,3,0,24,24};
      std::vector<double> released(n,-1);const auto begin=Clock::now();
      for(unsigned i=0;i<n;++i)c[i]->board->SetRaceParticipants(masks[i]);
      while(Clock::now()-begin<std::chrono::seconds(12)) {
        bool done=true;
        for(unsigned i=0;i<n;++i) {
          auto& b=*c[i]->board;const double t=std::chrono::duration<double>(Clock::now()-begin).count();
          const bool gate=masks[i] && released[i]<0 && t>=.1+i*.2;
          const bool held=b.HoldForRaceStart(gate);assert(b.m_state==State::ready);
          if(!held) {
            if(gate)released[i]=std::chrono::duration<double>(Clock::now()-begin).count();
            b.RunFrame();
          }
          if(!masks[i])assert(!held && !b.m_gameNet.m_startWaiting);
          else if(released[i]<0)done=false;
        }
        if(done)break;SleepForMs(2);
      }
      for(unsigned i:{0u,1u,3u,4u})assert(released[i]>0 && c[i]->board->m_gameNet.m_startParticipants==masks[i]);
      assert(std::abs(released[0]-released[1])<.1 && std::abs(released[3]-released[4])<.1);
      assert(released[2]<0 && errors==0);
      std::cout<<"PASS concurrent disjoint native cohorts on a five-cabinet physical ring with idle cabinet2\n";return 0;
    }
    if(scenario=="start-cohorts") {
      // Real transport, native rosters supplied by the ROM-free fixture.
      // Keep idle cabinets running, and deliberately vary coordinators and
      // local gate counts. Late selectors are members, not guessed spectators.
      assert(n>=3);
      const uint32_t last=1u<<(n-1), all=(1u<<n)-1;
      const std::vector<uint32_t> masks={last,2u|last,all,1u|last,last,all};
      for(uint32_t mask:masks) {
        std::vector<double> released(n,-1);
        for(auto& cab:c) { assert(!cab->board->HoldForRaceStart(false)); cab->board->SetRaceParticipants(mask); }
        const auto begin=Clock::now(); double lastGate=0;
        while(Clock::now()-begin<std::chrono::seconds(17)) {
          bool done=true;
          for(unsigned i=0;i<n;++i) {
            auto& b=*c[i]->board;
            const bool member=(mask&(1u<<i))!=0;
            const double elapsed=std::chrono::duration<double>(Clock::now()-begin).count();
            const double arrive=.1+i*.4;
            const bool gate=member && released[i]<0 && elapsed>=arrive;
            const auto counter=b.m_counter;
            const bool held=b.HoldForRaceStart(gate);
            assert(b.m_state==State::ready);
            if(!member) { assert(!held && !b.m_gameNet.m_startWaiting); }
            if(held) {
              assert(counter==b.m_counter);
              if(b.m_gameNet.m_startPrepared)b.SetRaceParticipants(all);
            }
            else {
              if(gate) released[i]=std::chrono::duration<double>(Clock::now()-begin).count();
              b.RunFrame();
            }
            if(member) { lastGate=std::max(lastGate,arrive); if(released[i]<0) done=false; }
          }
          if(done) break;
          SleepForMs(2);
        }
        double first=100, final=0;
        for(unsigned i=0;i<n;++i) if(mask&(1u<<i)) {
          assert(released[i]>=lastGate); first=std::min(first,released[i]); final=std::max(final,released[i]);
          assert(c[i]->board->m_gameNet.m_startParticipants==mask);
        } else assert(released[i]<0);
        assert(final-first<.1);
        for(auto& cab:c) assert(!cab->board->HoldForRaceStart(false));
        std::cout<<"PASS native-cohort mask="<<mask<<" physical="<<n<<" skew_ms="<<(final-first)*1000<<" idle cabinets never held\n";
      }
      assert(errors==0); return 0;
    }
    for (unsigned epoch = 1; epoch <= (scenario == "start-repeat" ? 2u : 1u); ++epoch)
    {
      for (auto& cab : c) cab->board->HoldForRaceStart(false);
      const auto begin = Clock::now();
      std::vector<double> release(n,-1);
      const double stagger = scenario == "start-late" ? 3.0 : .4;
      while (Clock::now()-begin < std::chrono::seconds(15))
      {
        for (unsigned i=0;i<n;++i)
        {
          auto& b=*c[i]->board;
          const double elapsed=std::chrono::duration<double>(Clock::now()-begin).count();
          const bool gate=release[i]<0 && elapsed >= .1 + i*stagger;
          const auto counter=b.m_counter;
          const auto before=Clock::now();
          const bool held=b.HoldForRaceStart(gate);
          assert(Clock::now()-before<std::chrono::milliseconds(100));
          assert(b.m_state == State::ready);
          if (held) assert(b.m_counter == counter);
          else
          {
            if (gate)
            {
              release[i]=std::chrono::duration<double>(Clock::now()-begin).count();
              assert(b.m_gameNet.m_startReleased && b.m_gameNet.m_startEpoch==epoch);
            }
            const auto raw=MakePacket(b.m_counter,float(i*100),0);
            for (unsigned bank : {0u,0x10000u}) memcpy(c[i]->buffer.data()+bank+0x100,raw.data(),raw.size());
            b.RunFrame();
          }
        }
        if (std::all_of(release.begin(),release.end(),[](double t){return t>=0;})) break;
        SleepForMs(2);
      }
      assert(std::all_of(release.begin(),release.end(),[](double t){return t>=0;}));
      const auto range=std::minmax_element(release.begin(),release.end());
      const double skew=*range.second-*range.first;
      assert(*range.first >= .1+(n-1)*stagger);
      assert(skew < .1); // scheduler tolerance, not a universal synchronization guarantee
      std::cout << "PASS " << scenario << " n=" << n << " epoch=" << epoch << " skew_ms=" << skew*1000 << " first_release_s=" << *range.first << '\n';
      // Explicitly rearm after the native state leaves the checkpoint.
      for (auto& cab:c) { assert(!cab->board->HoldForRaceStart(false)); assert(!cab->board->m_gameNet.m_startWaiting); }
    }
    assert(errors==0);
    c[0]->board->Reset(); assert(c[0]->board->m_gameNet.m_startEpoch==0);
    return 0;
  }
  if (poseCase)
  {
    const bool enabled = Dayto2pe::IsSupportedGame(game) && segment == 848 && n > 1;
    bool changed = false, cpuChanged = false;
    const unsigned compact = game == "daytona2" ? 0xd4 : 0xd0;
    auto pose = [&](unsigned owner, unsigned frame) {
      auto packet = MakePacket(uint16_t(frame), float(owner * 100 + frame * .05), float(-int(owner) * 20), game=="daytona2"?Dayto2pe::BattleOnTheEdge:Dayto2pe::PowerEdition);
      if (cpuCase) {
        packet[compact^2]=3; packet[(compact+1)^2]=0; packet[(compact+2)^2]=2; packet[(compact+3)^2]=1;
        for (unsigned j=0;j<2;++j) {
          Dayto2pe::WriteFloat(packet.data(),compact+4+24*j,float(owner*100+j*10+frame*.04));
          Dayto2pe::WriteFloat(packet.data(),compact+8+24*j,float(j*20));
        }
      }
      return packet;
    };
    if (cpuCase) for (unsigned i=0;i<n;++i) {
      Dayto2pe::CpuRouting routing; routing.local=i+1; routing.machines=n;
      for (unsigned owner=0;owner<n;++owner) {
        auto& m=routing.owners[owner+1];m.format=3;m.count=2;
        for(unsigned j=0;j<2;++j) { const unsigned id=n+2*owner+j; m.ids[j]=id; routing.types[id]=2; routing.authorities[id]=owner+1; }
      }
      c[i]->board->SetDaytona2CpuRouting(routing);
    }
    for (auto& cab : c) assert(!cab->board->m_gameNet.daytona.m_daytoPredictors.empty() == enabled);
    for (unsigned frame = 1; frame <= 600; ++frame)
    {
      for (unsigned i = 0; i < n; ++i)
      {
        const auto packet = pose(i,frame);
        for (unsigned bank : {0u, 0x10000u})
          memcpy(c[i]->buffer.data() + bank + 0x100, packet.data(), segment);
      }
      tick();
      for (unsigned i = 0; i < n; ++i)
      {
        auto& b = *c[i]->board;
        assert(b.m_state == State::ready);
        const auto local = pose(i,frame);
        for (unsigned byte = 0; byte < segment; ++byte)
          assert(b.ReadCommRAM8(0x100 + n * segment + byte) == local[byte]);
        for (unsigned slot = 0; slot < n; ++slot)
        {
          const auto raw = b.m_asyncLatest.data() + slot * segment;
          if (!Dayto2pe::Read32(raw, 4)) continue;
          const unsigned origin = (i + n - 1 - slot) % n;
          const unsigned stamp = Dayto2pe::Read16(raw, 0x3a);
          const auto original = pose(origin,stamp);
          // Proves forwarded snapshots (including the returning local one)
          // contain the producer's pose, never an intermediate predicted pose.
          assert(memcmp(raw, original.data(), segment) == 0);
          if (slot == n - 1) continue;
          for (unsigned byte = 0; byte < segment; ++byte)
          {
            const auto visible = b.ReadCommRAM8(0x100 + (slot + 1) * segment + byte);
            const unsigned off=byte^2;
            const bool cpuPosition=cpuCase && ((off>=compact+4&&off<compact+12)||(off>=compact+28&&off<compact+36));
            if (!enabled || ((!cpuPosition) && (byte < 0x6c || byte >= 0x74))) assert(visible == raw[byte]);
            else if (visible != raw[byte]) { changed = true; if(cpuPosition) cpuChanged=true; }
          }
        }
      }
    }
    assert(changed == enabled);
    assert(cpuChanged == (cpuCase && enabled));
    assert(errors == 0);
    c[0]->board->Reset();
    assert(c[0]->board->m_gameNet.daytona.m_daytoPredictors.empty());
    std::cout << "PASS " << scenario << ' ' << game << " cabinets=" << n
              << " raw forwarding/local echo/control bytes preserved; prediction=" << enabled << '\n';
    return 0;
  }
  for (unsigned i = 0; i < n; i++)
  {
    auto& b = *c[i]->board;
    assert(b.m_numMachines == n);
    assert(b.m_asyncIndex[0] == i);
    if (scenario == "relay")
    {
      assert(b.m_asyncCount[1] == n - 2);
      if (i == n - 1 && game != "dirtdvls") assert(b.m_status1 & 0x80);
    }
    // Both banks' local data, so it remains stable across swaps.
    for (unsigned bank : {0u, 0x10000u})
      std::fill(c[i]->buffer.begin() + bank + 0x100,
                c[i]->buffer.begin() + bank + 0x100 + segment, uint8_t(i + 1));
  }
  // Poll much faster than real time. High RTT must not gate counters/bank swaps.
  bool receivedAll = false;
  const auto exchangeDeadline = Clock::now() + std::chrono::seconds(12);
  unsigned frames = 0;
  while ((!receivedAll || frames < 400) && Clock::now() < exchangeDeadline)
  {
    std::vector<uint16_t> counters;
    std::vector<bool> banks;
    for (auto& cab : c) { counters.push_back(cab->board->m_counter); banks.push_back(cab->board->m_commbank); }
    tick();
    receivedAll = true;
    for (unsigned i = 0; i < n; i++)
    {
      auto& b = *c[i]->board;
      assert(b.m_state == State::ready);
      assert(b.m_counter == uint16_t(counters[i] + 1));
      assert(b.m_commbank != banks[i]);
      assert(b.ReadCommRAM16(6) == FLIPENDIAN16(b.m_counter));
      for (unsigned slot = 1; slot <= n; slot++)
      {
        const uint8_t expected = (i + n - slot) % n + 1;
        for (unsigned byte = 0; byte < segment; byte++)
          if (b.ReadCommRAM8(0x100 + slot * segment + byte) != expected) receivedAll = false;
      }
      // No unbounded frame backlog, regardless of how fast emulation runs.
      std::lock_guard<std::mutex> lock(b.m_link->m_mutex);
      assert(b.m_link->m_queue.size() <= 2 * n + 4);
    }
    frames++;
  }
  assert(receivedAll);
  assert(errors == 0);

  if (scenario == "sync")
  {
    c[0]->board->Reset();
    std::cout << "PASS legacy synchronized loopback\n";
    return 0;
  }

  // A paused peer must not stall local emulation, tear a snapshot, or alternate
  // stale banks. Also inspect the real transport's queue bound while stalled.
  for (unsigned frame = 0; frame < 300; frame++)
  {
    auto& b = *c[0]->board;
    const auto counter = b.m_counter;
    b.RunFrame();
    assert(b.m_counter == uint16_t(counter + 1));
    for (unsigned slot = 1; slot < n; slot++)
      assert(b.ReadCommRAM8(0x100 + slot * segment) == (n - slot) % n + 1);
  }
  for (auto& cab : c)
  {
    std::lock_guard<std::mutex> lock(cab->board->m_link->m_mutex);
    assert(cab->board->m_link->m_queue.size() <= 2 * n + 4);
  }

  if (scenario == "malformed" || scenario == "disconnect")
  {
    if (scenario == "malformed")
    {
      // Wrong-size ready packet, delivered by the real preceding TCP peer.
      const uint8_t bad[] = {1, 2, 3};
      c.back()->board->m_link->Send(bad, sizeof(bad));
    }
    else
    {
      c.back()->board->m_link->m_output->Close();
    }
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    while (!anyError() && Clock::now() < deadline) tick();
    assert(anyError());
    std::cout << "PASS " << scenario << " detected without blocking\n";
    return 0;
  }

  // Change the local payload while peers are paused. On resuming, a new
  // exchange must sample the latest value, not replay every missed frame.
  for (unsigned bank : {0u, 0x10000u})
    std::fill(c[0]->buffer.begin() + bank + 0x100,
              c[0]->buffer.begin() + bank + 0x100 + segment, uint8_t(0xa5));
  const auto updateDeadline = Clock::now() + std::chrono::seconds(5);
  bool updated = false;
  while (!updated && Clock::now() < updateDeadline)
  {
    tick();
    updated = true;
    for (unsigned i = 0; i < n; i++)
    {
      auto& b = *c[i]->board;
      assert(b.m_state == State::ready);
      for (unsigned slot = 1; slot <= n; slot++)
      {
        const unsigned origin = (i + n - slot) % n;
        const uint8_t value = b.ReadCommRAM8(0x100 + slot * segment);
        assert(value == origin + 1 || (origin == 0 && value == 0xa5));
        for (unsigned byte = 1; byte < segment; byte++)
          assert(b.ReadCommRAM8(0x100 + slot * segment + byte) == value);
        if (origin == 0 && value != 0xa5) updated = false;
      }
    }
  }
  assert(updated);

  // Force the wall-clock watchdog, without a 30-second test sleep.
  auto& first = *c[0]->board;
  first.m_asyncInFlight = true;
  first.m_asyncProgress = Clock::now() - std::chrono::seconds(31);
  assert(first.AsyncTimedOut());
  assert(first.m_state == State::error);
  // Reset must not wait for the ring acknowledgement (even with peers paused).
  auto& last = *c.back()->board;
  last.m_state = State::ready;
  last.CommRAM = last.Buffer + 0x10000;
  last.externalCommRAM = last.Buffer;
  last.m_commbank = true;
  const auto beforeReset = Clock::now();
  last.Reset();
  assert(Clock::now() - beforeReset < std::chrono::milliseconds(100));
  assert(last.m_asyncWorking.empty() && last.m_asyncLatest.empty());
  assert(!last.IsRunning());
  assert(last.CommRAM == last.Buffer && !last.m_commbank);
  // Re-initialization from an upper-bank reset must not clear past Buffer.
  last.WriteIORegister(0xc0, 1);
  last.RunFrame();
  last.WriteIORegister(0x88, 0xf000);
  last.RunFrame();
  assert(last.m_state == State::testing);
  assert(maxFrameMs < 100); // generous scheduler tolerance; not a benchmark
  std::cout << "PASS " << scenario << ' ' << game << " cabinets=" << n
            << " frames=" << frames << " max_RunFrame_ms=" << maxFrameMs << '\n';
}
