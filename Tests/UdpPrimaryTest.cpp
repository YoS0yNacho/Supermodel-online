// ROM-free test of UDP-primary racing through the real relay.py (TCP + UDP).
// Driven by Tests/run_udp_primary.py. GPL-3.0-or-later.
#include "ScudPacket.h"
#include "Srally2Packet.h"
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

static unsigned Segment = Scud::PacketSize; // 2048 for srally2 (set from argv before the cabinets exist)
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

// Sega Rally 2 through the relay: its menu/track selection barrier
// (0xFF84E1DC) reads the returning ring, including the cabinet's own echoed
// slot, and acknowledges with values that may be held for a single segment.
// What it needs from the link is therefore not the hop-by-hop ring itself but
// that every cabinet sees every segment of every slot, in order, one per
// frame. This checks exactly that while the relay fans segments out (star) or
// the receive thread forwards them (ring), then runs the barrier model from
// Tests/AsyncNetboardTest.cpp over the relay.
static int RunSrally2(std::vector<std::unique_ptr<Cabinet>>& c, const std::string& scenario, unsigned n)
{
  using Clock = std::chrono::steady_clock;
  const unsigned seg = Segment;
  constexpr unsigned PhaseOff = 0x44, AckOff = 0x6c, CounterOff = 0x14;
  std::vector<uint8_t> phase(n, 0), ack(n, 0);
  std::vector<uint32_t> counter(n, 0);
  unsigned frame = 0;
  // The local segment: owner fill byte, barrier words, and a counter that the
  // test advances every HoldFrames frames (several relay ticks), so every value
  // is sent at least once and every receiver must see every value in order.
  auto writeLocal = [&](unsigned i) {
    std::vector<uint8_t> tx(seg, uint8_t(0x10 + i));
    tx[PhaseOff] = phase[i]; tx[AckOff] = ack[i]; memcpy(&tx[CounterOff], &counter[i], 4);
    auto& b = *c[i]->board;
    if (b.m_state == State::ready)
      for (unsigned k = 0; k < seg; k += 4) { uint32_t w; memcpy(&w, tx.data() + k, 4); b.WriteCommRAM32(0x100 + k, w); }
    else for (unsigned bank : {0u, 0x10000u}) memcpy(c[i]->buffer.data() + bank + 0x100, tx.data(), seg);
  };
  auto slotByte = [&](unsigned i, unsigned slot, unsigned off) { return c[i]->board->ReadCommRAM8(0x100 + slot * seg + off); };
  auto slotWord = [&](unsigned i, unsigned slot, unsigned off) {
    uint32_t w = 0; for (unsigned k = 0; k < 4; ++k) w |= uint32_t(c[i]->board->ReadCommRAM8(0x100 + slot * seg + off + k)) << (8 * k); return w; };
  // Every observed counter value per (cabinet, slot) must follow the previous one by exactly 1.
  std::vector<std::vector<uint32_t>> seen(n, std::vector<uint32_t>(n + 1, 0));
  uint64_t steps = 0; bool checkCounters = false; unsigned maxLag = 0;
  auto tick = [&] {
    ++frame;
    for (unsigned i = 0; i < n; ++i)
    {
      auto& b = *c[i]->board;
      const int index = b.RelayCabinetIndex();
      if (index >= 0) { const uint16_t role = uint16_t(index); memcpy(c[i]->ram.data() + c[i]->Base(), &role, 2); }
      writeLocal(i);
      b.RunFrame();
      CHECK(b.m_state != State::error);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(8));
    if (!checkCounters) return;
    for (unsigned i = 0; i < n; ++i)
      for (unsigned slot = 1; slot <= n; ++slot)
      {
        const unsigned owner = (i + n - slot) % n;
        if (slotByte(i, slot, 0) != uint8_t(0x10 + owner)) continue;
        const uint32_t v = slotWord(i, slot, CounterOff);
        if (v == seen[i][slot]) continue;
        CHECK(v == seen[i][slot] + 1);          // no segment skipped, none out of order
        seen[i][slot] = v; ++steps;
        maxLag = std::max(maxLag, counter[owner] - v);
      }
  };
  auto run = [&](double seconds) { const auto end = Clock::now() + std::chrono::duration<double>(seconds); while (Clock::now() < end) tick(); };
  auto until = [&](double seconds, const std::function<bool()>& done) {
    const auto end = Clock::now() + std::chrono::duration<double>(seconds);
    while (!done() && Clock::now() < end) tick();
    return done();
  };
  auto all = [&](const std::function<bool(CSimNetBoard&)>& f) { return std::all_of(c.begin(), c.end(), [&](auto& cab) { return f(*cab->board); }); };

  CHECK(until(20, [&] { return all([](CSimNetBoard& b) { return b.m_state == State::ready; }); }));
  std::sort(c.begin(), c.end(), [](const auto& a, const auto& b) { return a->board->m_asyncIndex[0] < b->board->m_asyncIndex[0]; });
  for (unsigned i = 0; i < n; ++i) CHECK(c[i]->board->m_asyncIndex[0] == i);
  for (unsigned i = 0; i < n; ++i) CHECK(!c[i]->board->m_gameNet.ImmediateSelfEcho()); // the self slot comes back round the ring
  // Every slot, including the self echo, filled by the right cabinet.
  CHECK(until(10, [&] {
    for (unsigned i = 0; i < n; ++i) for (unsigned slot = 1; slot <= n; ++slot) if (slotByte(i, slot, 0) != uint8_t(0x10 + (i + n - slot) % n)) return false;
    return true; }));
  const bool star = scenario.find("star") != std::string::npos;
  CHECK(all([&](CSimNetBoard& b) { return b.m_link->RelayStar() == star; }));
  CHECK(all([](CSimNetBoard& b) { return !b.m_udpPrimary; })); // the ring never falls silent for Sega Rally 2
  for (unsigned i = 0; i < n; ++i) for (unsigned slot = 1; slot <= n; ++slot) seen[i][slot] = slotWord(i, slot, CounterOff);

  // 1. Counters: advance every HoldFrames frames for 4 s; nobody may miss a value.
  constexpr unsigned HoldFrames = 10; // ~80 ms here, several 60 Hz relay ticks
  checkCounters = true;
  const auto t0 = Clock::now();
  while (Clock::now() - t0 < std::chrono::seconds(4)) { if (frame % HoldFrames == 0) for (auto& v : counter) ++v; tick(); }
  checkCounters = false;
  CHECK(steps >= uint64_t(n) * (n + 1) * 30); // ~50 values per slot offered
  uint64_t rttSum = 0, rttCount = 0, early = 0;
  for (auto& cab : c) { rttSum += cab->board->m_statRingRttSumUs; rttCount += cab->board->m_statRingRttCount; early += cab->board->m_link->EarlyForwarded(); }
  CHECK(rttCount > 0);
  if (star) CHECK(early == 0); else if (n >= 3) CHECK(early > 0); // ring hops now leave from the receive thread

  // 2. Pulse: a value written for one segment is seen by a peer and comes back in the self echo.
  // (With the relay tick below the frame rate a single frame is not always sent; hold it for one tick.)
  // 3. Barrier (Tests/AsyncNetboardTest.cpp srally2-ring-echo): coordinator advances
  // phase 1 -> 2 -> 3 once every slot 1..n reports ack == phase; every cabinet
  // clears (phase, ack) on the first frame it sees ack == 3 in all slots
  // including its own echoed slot n.
  uint8_t coordPhase = 1;
  std::vector<bool> exited(n, false);
  phase[0] = coordPhase;
  const auto barrierStart = Clock::now();
  while (!std::all_of(exited.begin(), exited.end(), [](bool v) { return v; }) && Clock::now() - barrierStart < std::chrono::seconds(12))
  {
    tick();
    for (unsigned i = 0; i < n; ++i)
    {
      if (exited[i]) continue;
      const unsigned coordSlot = (i == 0) ? n : i;
      const uint8_t seenCoordPhase = (i == 0) ? coordPhase : slotByte(i, coordSlot, PhaseOff);
      if (seenCoordPhase > ack[i] && ack[i] < 3) ++ack[i];
      auto allSlotsAck = [&](uint8_t target) { for (unsigned slot = 1; slot <= n; ++slot) if (slotByte(i, slot, AckOff) != target) return false; return true; };
      if (i == 0 && coordPhase < 3 && allSlotsAck(coordPhase)) { ++coordPhase; phase[0] = coordPhase; }
      if (ack[i] == 3 && allSlotsAck(3)) { exited[i] = true; phase[i] = 0; ack[i] = 0; }
    }
  }
  CHECK(std::all_of(exited.begin(), exited.end(), [](bool v) { return v; }));
  const double barrierMs = std::chrono::duration<double, std::milli>(Clock::now() - barrierStart).count();

  // 4. Racing: real race packets for 3 s. The ring carries them as before and
  // the predictor takes its inputs from the UDP snapshots, which now come with
  // the relay-clock sample age like every other game's (they used to arrive
  // with an unknown age, which disabled the predictor's latency compensation).
  unsigned raceFrame = 0;
  auto raceTick = [&] {
    ++frame; ++raceFrame;
    for (unsigned i = 0; i < n; ++i)
    {
      auto& b = *c[i]->board;
      const auto p = MakeSrally2RacePacket(i, raceFrame, float(i * 40.0 + 0.85 * raceFrame), float(5.0 + 0.04 * raceFrame), float(i * 15.0 + 0.45 * raceFrame),
                                           0.85f, 0.04f, 0.45f, 64, int16_t(1000 + int(raceFrame * 24)), -32, 0.0023f, i == 0 ? 3u : 0u);
      for (unsigned k = 0; k < seg; k += 4) { uint32_t w; memcpy(&w, p.data() + k, 4); b.WriteCommRAM32(0x100 + k, w); }
      b.RunFrame();
      CHECK(b.m_state != State::error);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(8));
  };
  { const auto end = Clock::now() + std::chrono::seconds(3); while (Clock::now() < end) raceTick(); }
  uint64_t presented = 0, presentedTimed = 0;
  for (auto& cab : c) { presented += cab->board->m_statUdpPresentation; presentedTimed += cab->board->m_statUdpPresentationTimed; }
  CHECK(presented >= 20 * (n - 1) && presentedTimed * 10 >= presented * 9);
  CHECK(all([](CSimNetBoard& b) { return !b.m_udpPrimary; }));
  CHECK(errors == 0);
  std::cout << "INFO " << scenario << " cabinets=" << n << " counter steps " << steps << ", worst lag " << maxLag << " values; racing: " << presented
            << " predictor inputs from UDP, " << presentedTimed << " with a relay-clock age\n";
  std::cout << "PASS " << scenario << " cabinets=" << n << ": " << (star ? "relay star fan-out" : "TCP ring") << ", no slot ever skipped a segment, barrier 1->2->3 cleared on every cabinet in "
            << unsigned(barrierMs) << " ms, mean ring round trip " << (rttSum / rttCount / 1000) << " ms, " << early << " hops forwarded by the receive thread\n";
  return 0;
}

int main(int argc, char** argv)
{
  setvbuf(stdout, nullptr, _IONBF, 0);
  CHECK(argc >= 6);
  const std::string scenario = argv[1], game = argv[2], relay = argv[3];
  const unsigned n = unsigned(argc - 4);
  if (game == "srally2") Segment = 2048;
  std::vector<std::unique_ptr<Cabinet>> c;
  for (unsigned i = 0; i < n; ++i) c.emplace_back(new Cabinet(game, relay, argv[4 + i]));
  if (game == "srally2") return RunSrally2(c, scenario, n);
  unsigned frame = 0, phase = 21, maxStale = 0, control = 0;
  uint64_t ageSumUs = 0, ageCount = 0; // UDP sample age at apply time while UDP carries the race
  uint64_t tcpAgeSumUs = 0, tcpUncSumUs = 0, tcpAgeCount = 0; // timed-segment age/uncertainty while TCP carries it
  std::vector<unsigned> staleSamples; bool collectStale = false;   // per-slot staleness (frames) of every tick while collecting
  constexpr size_t ControlOffset = 0x300; // a quiet byte: not a position, not race context
  std::vector<std::vector<unsigned>> controlSeen(n, std::vector<unsigned>(n, 0));
  bool ready = false;
  auto packet = [&](unsigned owner, unsigned f, unsigned ph, unsigned ctl) {
    auto p = ScudPacket(owner, f, n, 1);
    p[0x1f ^ 2] = p[0x21 ^ 2] = uint8_t(ph);
    p[ControlOffset] = uint8_t(ctl);
    // Real segments are mostly non-zero data that rarely changes (CPU car tables,
    // settings). Fill the quiet half of the packet that way so the zero-run
    // encoding alone cannot shrink it; only the keyframe delta can.
    for (unsigned i = 0x200; i < Segment; ++i)
      if (i != ControlOffset) p[i] = uint8_t(1 + (i * 7 + owner * 13) % 251);
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
        const auto p = packet(unsigned(index) + 1, frame, phase, control);
        if (b.m_state == State::ready)
        {
          // Like the game: the packet goes into the bank the CPU sees, through
          // the bus hooks, so the board knows when the segment was written.
          for (unsigned k = 0; k < p.size(); k += 4) { uint32_t w; memcpy(&w, p.data() + k, 4); b.WriteCommRAM32(0x100 + k, w); }
        }
        else for (unsigned bank : {0u, 0x10000u}) memcpy(c[i]->buffer.data() + bank + 0x100, p.data(), p.size());
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
        const unsigned ctl = raw[ControlOffset];
        const auto original = packet(owner, stamp, Scud::Read8(raw, 0x1f), ctl);
        CHECK(!memcmp(raw, original.data(), Segment));
        // Control values must arrive in order with none skipped.
        CHECK(ctl == controlSeen[i][slot] || ctl == controlSeen[i][slot] + 1);
        controlSeen[i][slot] = ctl;
        CHECK(stamp <= frame);
        maxStale = std::max(maxStale, frame - stamp);
        if (collectStale) staleSamples.push_back(frame - stamp);
        CHECK(b.m_asyncLatestAgeUs[slot] < 1500000);
        if (b.m_udpPrimary && b.m_asyncLatestUncertaintyUs[slot] < 5000) { ageSumUs += b.m_asyncLatestAgeUs[slot]; ++ageCount; }
        if (!b.m_udpPrimary) { tcpAgeSumUs += b.m_asyncLatestAgeUs[slot]; tcpUncSumUs += b.m_asyncLatestUncertaintyUs[slot]; ++tcpAgeCount; }
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
  // Delta lane: nearly every snapshot must be a delta, and the saving must be
  // large even though half of the segment is static non-zero data.
  auto deltaStats = [&](const char* label) {
    for (unsigned i = 0; i < n; ++i)
    {
      CNetBoardLink::UdpStats u;
      CHECK(c[i]->board->m_link->GetUdpStats(u));
      CHECK(u.sent > 0 && u.sentDelta * 10 >= u.sent * 9);
      CHECK(u.sentBytes * 4 <= u.sentRawBytes);            // at least 75% smaller than raw
      CHECK(u.received > 0 && u.deltaStale * 20 <= u.received);
      std::cout << "INFO " << label << " cabinet " << i << " udp sent=" << u.sent << " deltas=" << u.sentDelta
                << " bytes=" << u.sentRawBytes << "->" << u.sentBytes << " (" << (u.sentBytes * 100 / std::max<uint64_t>(1, u.sentRawBytes))
                << "% of raw, avg " << u.sentBytes / std::max<uint64_t>(1, u.sent) << " B/snapshot) received=" << u.received
                << " waitedForKeyframe=" << u.deltaBaseUnknown << " resolvedLater=" << u.heldResolved << " stale=" << u.deltaStale
                << " masksSent=" << u.maskSent << " masksReceived=" << u.maskReceived << "\n";
      CHECK(u.maskSent > 0 && u.maskReceived > 0);
    }
  };

  CHECK(until(20, [&] { return all([](CSimNetBoard& b) { return b.m_state == State::ready; }); }));
  std::sort(c.begin(), c.end(), [](const auto& a, const auto& b) { return a->board->m_asyncIndex[0] < b->board->m_asyncIndex[0]; });
  for (unsigned i = 0; i < n; ++i) { CHECK(c[i]->board->m_asyncIndex[0] == i); c[i]->board->SetScudRouting(ScudRoute(i + 1, n, n, 1)); }
  ready = true;

  const bool ring = scenario.rfind("ring", 0) == 0; // TCP-only ring, early-forwarding measurement
  const bool star = scenario.rfind("star", 0) == 0; // TCP-only, relay star fan-out measurement
  if (scenario == "tcp-only" || scenario == "fallback" || ring || star)
  {
    // No UDP at all, or one cabinet that cannot receive UDP: the ring must keep running.
    run(2); maxStale = 0; tcpAgeSumUs = tcpUncSumUs = tcpAgeCount = 0;
    const auto t0 = tcp();
    const auto end = Clock::now() + std::chrono::seconds(4);
    while (Clock::now() < end) { tick(); CHECK(!any([](CSimNetBoard& b) { return b.m_udpPrimary; })); }
    const auto t1 = tcp(); const auto u1 = udp();
    for (unsigned i = 0; i < n; ++i) { CHECK(t1[i] - t0[i] >= 50); if (scenario == "tcp-only") CHECK(u1[i] == 0); }
    if (scenario == "fallback") CHECK(u1[1] == 0 && u1[0] > 0); // cabinet 2 is the one cut off from UDP
    CHECK(maxStale <= 60 && errors == 0);
    uint64_t rttSum = 0, rttCount = 0, early = 0;
    for (auto& cab : c) { rttSum += cab->board->m_statRingRttSumUs; rttCount += cab->board->m_statRingRttCount; early += cab->board->m_link->EarlyForwarded(); }
    const uint64_t meanRttMs = rttCount ? rttSum / rttCount / 1000 : 0;
    const bool fanOut = all([](CSimNetBoard& b) { return b.m_link->RelayStar(); });
    CHECK(fanOut == !ring); // ring* joins as pre-star lobby clients, everything else gets RLS1
    if (scenario == "ring")
    {
      // Peer hops are forwarded by the receive thread, so a segment's trip round
      // a 4-cabinet ring costs network time plus one frame at the origin, not one
      // frame per hop (which was ~32 ms here before early forwarding).
      CHECK(early > 0 && rttCount > 0 && meanRttMs <= 20);
    }
    if (star)
    {
      // The relay delivers every segment to every cabinet itself: nothing is
      // forwarded, and with 40 ms per delivery the farthest cabinet and the
      // origin's echo both see one delivery (~48 ms) instead of three or four.
      CHECK(early == 0 && rttCount > 0);
      CHECK(scenario == "star" ? meanRttMs <= 20 : (meanRttMs <= 80 && maxStale * 8 <= 100));
      // The relay translated the timestamps through its own clock (it pings
      // every cabinet once a second), so receivers still get confident ages:
      // ~21 ms here against ~20 ms on the ring (17 ms of that is the origin's
      // frame), and ~62 ms against ~100 ms with 40 ms per delivery.
      CHECK(tcpAgeCount > 0 && tcpUncSumUs / tcpAgeCount < (scenario == "star" ? 30000 : 80000));
    }
    // The origin stamps the segment with the time the game wrote it (the
    // write burst spans well under a millisecond here) instead of a whole
    // frame of uncertainty: ~3 ms mean on localhost where it was ~20 ms.
    if (scenario == "tcp-only" || scenario == "ring" || scenario == "star") CHECK(tcpAgeCount > 0 && tcpUncSumUs / tcpAgeCount < 8000);
    std::cout << "INFO " << scenario << " cabinets=" << n << " mean timed-segment age at apply " << (tcpAgeCount ? tcpAgeSumUs / tcpAgeCount : 0)
              << " us, mean uncertainty " << (tcpAgeCount ? tcpUncSumUs / tcpAgeCount : 0) << " us over " << tcpAgeCount << " samples\n";
    std::cout << "PASS " << scenario << " cabinets=" << n << ": TCP " << (fanOut ? "star fan-out" : "ring") << " carried the race, max staleness " << maxStale * 8
              << " ms, mean ring round trip " << meanRttMs << " ms, " << early << " hops forwarded by the receive thread\n";
    finished = true;
    return 0;
  }

  if (scenario == "netem")
  {
    // A race over an impaired network (Tests/netem.py profile named in the
    // environment): loss bursts, jitter, reordering, duplicates on UDP, delay
    // and jitter on TCP. The lane must still settle into UDP-only racing and
    // stay there, control values must arrive in order, and staleness must stay
    // within the profile's bound; the receivers' loss count is compared with
    // what the rig really dropped by the Python side.
    const char* profile = getenv("NETEM_PROFILE") ? getenv("NETEM_PROFILE") : "?";
    const unsigned maxStaleMs = getenv("NETEM_MAX_STALE_MS") ? unsigned(atoi(getenv("NETEM_MAX_STALE_MS"))) : 300;
    const uint64_t maxDrops = getenv("NETEM_MAX_DROPS") ? uint64_t(atoi(getenv("NETEM_MAX_DROPS"))) : 0;
    CHECK(until(15, silent));
    run(0.5); maxStale = 0; collectStale = true;
    const auto t0 = tcp(); const auto u0 = udp();
    run(1.5);
    for (control = 1; control <= 16; ++control) run(0.2);
    control = 16; run(1.5);
    const auto t1 = tcp(); const auto u1 = udp();
    collectStale = false;
    std::sort(staleSamples.begin(), staleSamples.end());
    auto pct = [&](double p) { return staleSamples.empty() ? 0u : staleSamples[std::min(staleSamples.size() - 1, size_t(p * staleSamples.size()))] * 8; };
    CNetBoardLink::UdpStats sum;
    uint64_t drops = 0;
    for (unsigned i = 0; i < n; ++i)
    {
      auto& b = *c[i]->board;
      for (unsigned slot = 0; slot + 1 < n; ++slot) CHECK(controlSeen[i][slot] == 16);
      CHECK(t1[i] - t0[i] >= 16);                      // one TCP control message per change
      CHECK(u1[i] - u0[i] >= 60 * (n - 1));            // positions kept flowing by UDP (~124 ticks offered)
      drops += b.m_statUdpPrimaryDrops;
      CNetBoardLink::UdpStats u; CHECK(b.m_link->GetUdpStats(u));
      sum.datagrams += u.datagrams; sum.lost += u.lost; sum.reordered += u.reordered; sum.duplicated += u.duplicated;
      std::cout << "INFO netem " << profile << " cabinet " << i << " tcp=" << (t1[i] - t0[i]) << " udp=" << (u1[i] - u0[i])
                << " drops=" << b.m_statUdpPrimaryDrops << " snapshots=" << u.datagrams << " lost=" << u.lost
                << " reordered=" << u.reordered << " duplicated=" << u.duplicated << " heldForControl=" << b.m_statUdpHeldForControl
                << " merged=" << b.m_statUdpMerged << "\n";
    }
    CHECK(drops <= maxDrops);
    CHECK(errors == 0 && maxStale * 8 <= maxStaleMs);
    std::cout << "STAT datagrams=" << sum.datagrams << " lost=" << sum.lost << " reordered=" << sum.reordered << " duplicated=" << sum.duplicated << "\n";
    std::cout << "PASS netem " << profile << " cabinets=" << n << ": UDP-only racing held (" << drops << " fallbacks), 16 control values in order, staleness p50/p95/p99/max "
              << pct(0.5) << "/" << pct(0.95) << "/" << pct(0.99) << "/" << maxStale * 8 << " ms, measured loss "
              << (sum.datagrams + sum.lost ? 100 * sum.lost / (sum.datagrams + sum.lost) : 0) << "%\n";
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
    deltaStats("primary");
    uint64_t transitSum = 0, transitCount = 0;
    for (auto& cab : c) { transitSum += cab->board->m_statUdpTransitSumUs; transitCount += cab->board->m_statUdpTransitCount; }
    // Snapshots are sent by the submitting thread and read by a blocking worker:
    // on localhost a snapshot reaches the peer through relay.py in well under a
    // millisecond (it was ~2.3 ms with the 2 ms polling worker).
    CHECK(transitCount > 0 && transitSum / transitCount < 1500);
    // The age at apply counts from the game's write (one 8 ms frame before the
    // snapshot was taken here), not from the pick-up: ~12 ms, where the
    // pick-up stamp alone said ~4 ms.
    CHECK(ageCount > 0 && ageSumUs / ageCount >= 7000 && ageSumUs / ageCount <= 30000);
    for (auto& cab : c) CHECK(cab->board->m_statUdpPrimaryDrops == 0);
    std::cout << "INFO primary cabinets=" << n << " mean UDP transit (stamp -> arrival) " << (transitCount ? transitSum / transitCount : 0)
              << " us, mean age at apply " << (ageCount ? ageSumUs / ageCount : 0) << " us over " << ageCount << " samples\n";
    std::cout << "PASS primary cabinets=" << n << ": 3 s of racing with 0 TCP segments, steady staleness " << steadyStale * 8
              << " ms, state change sent as " << (t2[0] - t1[0]) << " TCP keyframe(s)\n";
    finished = true;
    return 0;
  }
  if (scenario == "control" || scenario == "latency")
  {
    // 16 short-lived control values while the relay drops 15% of UDP snapshots.
    // Each one must reach every cabinet, in order, as a TCP control message,
    // and positions must keep flowing by UDP.
    std::cout << "CTL loss 0.15\n"; run(0.5); maxStale = 0;
    const auto u2 = udp();
    std::vector<uint64_t> held0; for (auto& cab : c) held0.push_back(cab->board->m_statUdpHeldForControl);
    for (control = 1; control <= 16; ++control) run(0.16);
    control = 16; run(0.5);
    const auto t2 = tcp(); const auto u3 = udp();
    for (unsigned i = 0; i < n; ++i)
    {
      for (unsigned slot = 0; slot + 1 < n; ++slot) CHECK(controlSeen[i][slot] == 16);
      auto& b = *c[i]->board;
      std::cout << "INFO cabinet " << i << " tcp=" << (t2[i] - t1[i]) << " udp=" << (u3[i] - u2[i]) << " held=" << (b.m_statUdpHeldForControl - held0[i])
                << " merged=" << b.m_statUdpMerged << " tcpControlMerged=" << b.m_statTcpControlMerged << " lane=" << b.m_udpPrimary << "\n";
      CHECK(t2[i] - t1[i] >= 16);                          // at least one TCP message per control change
      // Without latency the ring must stay quiet apart from those; with 80 ms
      // latency a loss streak may legitimately resume it for a second.
      if (scenario == "control") CHECK(t2[i] - t1[i] <= 40);
      CHECK(u3[i] - u2[i] >= 25 * (n - 1));                // positions still streamed by UDP
      // Once masks are known nothing is held: an early snapshot is applied with
      // its streamed words only. On localhost TCP usually lands before the next
      // UDP tick, so merges are only guaranteed in the latency scenario.
      CHECK(b.m_statUdpHeldForControl == held0[i]);
      if (scenario == "latency") CHECK(b.m_statUdpMerged > 0);
    }
    if (scenario == "latency")
    {
      // With 80 ms one-way fake latency the TCP control message reaches the far
      // cabinet ~320 ms after the UDP snapshots built on it. Those snapshots must
      // have been applied (streamed words) and the late TCP segment must then have
      // merged its control words: the in-order CHECK above proves none was skipped.
      uint64_t lateMerges = 0;
      for (unsigned i = 0; i < n; ++i) lateMerges += c[i]->board->m_statTcpControlMerged;
      CHECK(lateMerges > 0);
      CHECK(maxStale <= 60 && errors == 0); // remote cars kept moving at UDP pace, never frozen for the TCP trip
      std::cout << "PASS latency cabinets=" << n << ": 16 control values in order under 15% loss and 80 ms one-way latency; "
                << lateMerges << " late TCP control segments merged, worst staleness " << maxStale * 8 << " ms\n";
      finished = true;
      return 0;
    }
    CHECK(maxStale <= 60 && errors == 0);
    deltaStats("control");
    std::cout << "PASS control cabinets=" << n << ": 16 control values delivered in order under 15% UDP loss, "
              << (t2[0] - t1[0]) << " TCP messages, worst staleness " << maxStale * 8 << " ms\n";
    finished = true;
    return 0;
  }
  if (scenario == "stutter")
  {
    // The relay stops delivering UDP to cabinet 2 for 250 ms: longer than the
    // 175 ms a peer may stay quiet before the lane is confirmed, shorter than
    // the 400 ms it may stay quiet once it is. Nobody may fall back to TCP, so
    // the stall costs 250 ms of staleness and not a burst of keyframes.
    maxStale = 0;
    std::cout << "CTL block\n"; run(0.25); std::cout << "CTL unblock\n"; run(1.5);
    const auto t2 = tcp();
    for (unsigned i = 0; i < n; ++i) { CHECK(t2[i] == t1[i]); CHECK(c[i]->board->m_statUdpPrimaryDrops == 0); }
    CHECK(silent() && maxStale * 8 >= 200 && maxStale * 8 <= 400 && errors == 0);
    std::cout << "PASS stutter cabinets=" << n << ": 250 ms UDP stall left the lane UDP only (0 TCP segments, worst staleness "
              << maxStale * 8 << " ms)\n";
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
    for (unsigned i = 0; i < n; ++i) { CHECK(t5[i] == t4[i]); CHECK(c[i]->board->m_statUdpPrimaryDrops == 1); }
    CHECK(silent() && maxStale <= 40 && errors == 0);
    std::cout << "PASS recover cabinets=" << n << ": UDP outage fell back to TCP (worst staleness " << outageStale * 8
              << " ms), then returned to UDP only\n";
    finished = true;
    return 0;
  }
  CHECK(!"unknown scenario");
}
