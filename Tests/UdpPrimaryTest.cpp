// ROM-free test of UDP-primary racing through the real relay.py (TCP + UDP).
// Driven by Tests/run_udp_primary.py. GPL-3.0-or-later.
#include "ScudPacket.h"
#include "Network/Games/Policy.h"
#include "Supermodel.h"
#include "Util/NewConfig.h"
#include "Network/NetServer.h"
#include "Model3/INetBoard.h"
#include "OSD/Thread.h"
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>
// Test-only inspection; no test hooks in the production API.
#define private public
#include "Model3/SimNetBoard.h"
#undef private
#include <algorithm>
#include <cstdarg>
#include <cstdlib>
#include <iostream>
#include <thread>

using Clock = std::chrono::steady_clock;
static std::atomic<unsigned> errors{0};
static std::atomic<bool> finished{false}; // teardown closes links in arbitrary order
Result ErrorLog(const char* fmt, ...)
{
  if (finished) return Result::FAIL;
  ++errors;
  va_list args; va_start(args, fmt); vfprintf(stderr, fmt, args); va_end(args); fputc('\n', stderr);
  return Result::FAIL;
}
void InfoLog(const char* fmt, ...)
{
  if (!strstr(fmt, "Relay UDP")) return;
  va_list args; va_start(args, fmt); vfprintf(stdout, fmt, args); va_end(args); fputc('\n', stdout);
}
void DebugLog(const char*, ...) {}
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "CHECK FAILED line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

constexpr unsigned Segment = Scud::PacketSize;
struct Cabinet
{
  Util::Config::Node config{"test"};
  std::vector<uint8_t> ram = std::vector<uint8_t>(0x10000);
  std::vector<uint8_t> buffer = std::vector<uint8_t>(0x20000);
  std::shared_ptr<CNetServer> server;
  std::unique_ptr<CSimNetBoard> board;
  unsigned Base() const { return board->m_gameType == GameType::one ? 0x400 : 0x200; }
  Cabinet(const std::string& game, const std::string& relay, const std::string& ticket)
  {
    config.Set("Network", true);
    config.Set("NetSrally2Eight", false);
    config.Set("NetRelay", relay);
    config.Set("NetRelayTicket", ticket);
    config.Set("AsyncNet", true);
    config.Set("PortIn", 1970u);
    config.Set("PortOut", 1970u);
    config.Set("AddressOut", std::string("127.0.0.1"));
    server = std::make_shared<CNetServer>(1970, "UDP primary test", false);
    CHECK(server->Start());
    board = std::make_unique<CSimNetBoard>(config, server);
    Game info; info.name = game; if (game == "scudplus") info.parent = "scud";
    info.netboard_present = true;
    board->GetGame(info);
    CHECK(board->Init(ram.data(), buffer.data()) == Result::OKAY);
    board->SetRelayNativeReady();
    uint16_t value = Segment;
    memcpy(ram.data() + Base() + 4, &value, 2);
    board->WriteIORegister(0xc0, 1); board->RunFrame();
    board->WriteIORegister(0x88, 0xf000); board->RunFrame();
    CHECK(board->m_state == State::testing);
  }
};

int main(int argc, char** argv)
{
  setvbuf(stdout, nullptr, _IONBF, 0);
  CHECK(argc >= 6);
  const std::string scenario = argv[1], game = argv[2], relay = argv[3];
  const unsigned n = unsigned(argc - 4);
  std::vector<std::unique_ptr<Cabinet>> c;
  for (unsigned i = 0; i < n; ++i) c.emplace_back(new Cabinet(game, relay, argv[4 + i]));
  unsigned frame = 0, phase = 21, maxStale = 0;
  bool ready = false;
  auto packet = [&](unsigned owner, unsigned f, unsigned ph) {
    auto p = ScudPacket(owner, f, n, 1);
    p[0x1f ^ 2] = p[0x21 ^ 2] = uint8_t(ph);
    ScudChecksum(p);
    return p;
  };
  // One emulated frame for every cabinet, ~125 frames per second.
  auto tick = [&] {
    ++frame;
    for (unsigned i = 0; i < n; ++i)
    {
      auto& b = *c[i]->board;
      const int index = b.RelayCabinetIndex();
      if (index < 0) continue; // the relay has not assigned this cabinet yet
      {
        const uint16_t role = index == 0 ? 0 : 1;
        memcpy(c[i]->ram.data() + c[i]->Base(), &role, 2);
        const auto p = packet(unsigned(index) + 1, frame, phase);
        for (unsigned bank : {0u, 0x10000u}) memcpy(c[i]->buffer.data() + bank + 0x100, p.data(), p.size());
      }
      b.RunFrame();
      CHECK(b.m_state != State::error);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(8));
    if (!ready) return;
    // Every remote slot must hold an exact, recent packet from the right cabinet.
    for (unsigned i = 0; i < n; ++i)
    {
      auto& b = *c[i]->board;
      for (unsigned slot = 0; slot + 1 < n; ++slot)
      {
        const uint8_t* raw = b.m_asyncLatest.data() + slot * Segment;
        uint32_t stamp = 0;
        if (!Scud::DecodeHeader(raw, Segment, stamp)) continue; // nothing received yet
        const unsigned owner = 1 + (i + n - 1 - slot) % n;
        const auto original = packet(owner, stamp, Scud::Read8(raw, 0x1f));
        CHECK(!memcmp(raw, original.data(), Segment));
        CHECK(stamp <= frame);
        maxStale = std::max(maxStale, frame - stamp);
        CHECK(b.m_asyncLatestAgeUs[slot] < 1500000);
      }
    }
  };
  auto run = [&](double seconds) { const auto end = Clock::now() + std::chrono::duration<double>(seconds); while (Clock::now() < end) tick(); };
  auto until = [&](double seconds, const std::function<bool()>& done) {
    const auto end = Clock::now() + std::chrono::duration<double>(seconds);
    while (!done() && Clock::now() < end) tick();
    return done();
  };
  auto all = [&](const std::function<bool(CSimNetBoard&)>& f) { return std::all_of(c.begin(), c.end(), [&](auto& cab) { return f(*cab->board); }); };
  auto any = [&](const std::function<bool(CSimNetBoard&)>& f) { return std::any_of(c.begin(), c.end(), [&](auto& cab) { return f(*cab->board); }); };
  auto silent = [&] { return all([](CSimNetBoard& b) { return b.m_udpPrimary; }); };
  auto tcp = [&] { std::vector<uint64_t> v; for (auto& cab : c) v.push_back(cab->board->m_statTcpOriginated); return v; };
  auto udp = [&] { std::vector<uint64_t> v; for (auto& cab : c) v.push_back(cab->board->m_statUdpApplied); return v; };

  CHECK(until(20, [&] { return all([](CSimNetBoard& b) { return b.m_state == State::ready; }); }));
  std::sort(c.begin(), c.end(), [](const auto& a, const auto& b) { return a->board->m_asyncIndex[0] < b->board->m_asyncIndex[0]; });
  for (unsigned i = 0; i < n; ++i) { CHECK(c[i]->board->m_asyncIndex[0] == i); c[i]->board->SetScudRouting(ScudRoute(i + 1, n, n, 1)); }
  ready = true;

  if (scenario == "tcp-only" || scenario == "fallback")
  {
    // No UDP at all, or one cabinet that cannot receive UDP: the ring must keep running.
    run(2); maxStale = 0;
    const auto t0 = tcp();
    const auto end = Clock::now() + std::chrono::seconds(4);
    while (Clock::now() < end) { tick(); CHECK(!any([](CSimNetBoard& b) { return b.m_udpPrimary; })); }
    const auto t1 = tcp(); const auto u1 = udp();
    for (unsigned i = 0; i < n; ++i) { CHECK(t1[i] - t0[i] >= 50); if (scenario == "tcp-only") CHECK(u1[i] == 0); }
    if (scenario == "fallback") CHECK(u1[1] == 0 && u1[0] > 0); // cabinet 2 is the one cut off from UDP
    CHECK(maxStale <= 60 && errors == 0);
    std::cout << "PASS " << scenario << " cabinets=" << n << ": TCP ring carried the race, max staleness " << maxStale * 8 << " ms\n";
    finished = true;
    return 0;
  }

  CHECK(until(8, silent));
  run(0.5); maxStale = 0;
  auto t0 = tcp(); auto u0 = udp();
  run(3);
  auto t1 = tcp(); auto u1 = udp();
  for (unsigned i = 0; i < n; ++i)
  {
    CHECK(t1[i] == t0[i]);                       // not one TCP segment during the race
    CHECK(u1[i] - u0[i] >= 30 * (n - 1));        // every remote car still updated ~20 times a second
    for (unsigned slot = 0; slot + 1 < n; ++slot) CHECK(c[i]->board->m_asyncLatestUncertaintyUs[slot] < 1000000);
  }
  CHECK(silent() && maxStale <= 40);
  const unsigned steadyStale = maxStale;

  if (scenario == "primary")
  {
    // A race-state change must travel by TCP as a keyframe, then silence resumes.
    phase = 20; maxStale = 0;
    CHECK(until(2, [&] { return all([&](CSimNetBoard& b) {
      for (unsigned slot = 0; slot + 1 < n; ++slot) if (Scud::Read8(b.m_asyncLatest.data() + slot * Segment, 0x1f) != 20) return false;
      return true; }); }));
    run(1.5);
    const auto t2 = tcp();
    for (unsigned i = 0; i < n; ++i) CHECK(t2[i] - t1[i] >= 1 && t2[i] - t1[i] <= 4);
    CHECK(silent() && maxStale <= 60 && errors == 0);
    std::cout << "PASS primary cabinets=" << n << ": 3 s of racing with 0 TCP segments, steady staleness " << steadyStale * 8
              << " ms, state change sent as " << (t2[0] - t1[0]) << " TCP keyframe(s)\n";
    finished = true;
    return 0;
  }
  if (scenario == "recover")
  {
    // The relay stops delivering UDP to cabinet 2 mid-race, then restores it.
    std::cout << "CTL block\n"; maxStale = 0;
    CHECK(until(3, [&] { return !any([](CSimNetBoard& b) { return b.m_udpPrimary; }); }));
    const auto t2 = tcp();
    run(3);
    const auto t3 = tcp();
    for (unsigned i = 0; i < n; ++i) CHECK(t3[i] - t2[i] >= 40);
    CHECK(!any([](CSimNetBoard& b) { return b.m_udpPrimary; }));
    const unsigned outageStale = maxStale;
    CHECK(outageStale <= 150);                   // remote cars froze for at most 1.2 s
    std::cout << "CTL unblock\n";
    CHECK(until(8, silent));
    run(0.5); maxStale = 0;
    const auto t4 = tcp(); run(2); const auto t5 = tcp();
    for (unsigned i = 0; i < n; ++i) CHECK(t5[i] == t4[i]);
    CHECK(silent() && maxStale <= 40 && errors == 0);
    std::cout << "PASS recover cabinets=" << n << ": UDP outage fell back to TCP (worst staleness " << outageStale * 8
              << " ms), then returned to UDP only\n";
    finished = true;
    return 0;
  }
  CHECK(!"unknown scenario");
}
