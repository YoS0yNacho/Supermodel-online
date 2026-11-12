// Read-only, exact-ROM native standings. No pose/coordinate approximations.
// GPL-3.0-or-later
#ifndef INCLUDED_ONLINE_RACESTANDINGS_H
#define INCLUDED_ONLINE_RACESTANDINGS_H
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
namespace Online {
// A verified race/results phase can temporarily have no usable rank (rank=0).
// `results` is local lifecycle information, NOT a new flag on the OLR1 wire.
struct NativeStanding {
  bool racing = false;
  uint32_t rank = 0;
  bool results = false;
  bool Visible() const { return racing || results; }
};
inline NativeStanding StandingReport(NativeStanding s) {
  // Keep the existing bounded/authenticated side channel, including its TTL.
  // Results continue sending their native/retained rank at the normal 5 Hz;
  // an unknown rank still sends exactly (0,0), never malformed (0,1).
  return s.Visible() && s.rank>=1 && s.rank<=40
    ? NativeStanding{true,s.rank} : NativeStanding{};
}

// Per-emulator-session lifecycle, updated only between completed frames.
// Retain the last usable native position only inside a verified results phase,
// never across menus, attract, reset, selection or a new race/stage.
class StandingLifecycle {
public:
  void Reset() { m_seenRace=false; m_results=false; m_lastRank=0; }
  NativeStanding Update(NativeStanding sample) {
    const uint32_t rank = sample.rank>=1 && sample.rank<=40 ? sample.rank : 0;
    if (sample.results && !sample.racing) {
      if (!m_seenRace) return {}; // not an attract/replayed results board
      m_results=true;
      if (rank) m_lastRank=rank; // accept native final-sort updates too
      return {false,m_lastRank,true};
    }
    if (sample.racing && !sample.results) {
      if (m_results) m_lastRank=0; // direct stage restart, even without a menu
      m_seenRace=true;
      m_results=false;
      if (rank) m_lastRank=rank;
      return {true,rank}; // unknown LIVE ranks must not become cached ranks
    }
    Reset();
    return {};
  }
private:
  bool m_seenRace=false, m_results=false;
  uint32_t m_lastRank=0;
};
struct StandingRow { uint32_t car = 0, rank = 0; std::string name; };
// Templated for deterministic memory-fixture tests without an emulator/ROM.
// Call ONLY on the emulation thread, between completed frames.
template<class Memory> NativeStanding ReadNativeStanding(Memory& mem, const std::string& game) {
  uint32_t rank = 0;
  bool results = false;
  if (game == "daytona2" || game == "dayto2pe") {
    const unsigned phase = mem.Read8(0x105004);
    // Exact native dispatch: 18/19 finish display; PE also has a separate
    // linked result board at28/29 (original Daytona does NOT).
    // In particular do NOT include 20..27 (name entry / records / menus).
    results = phase==18 || phase==19 || (game=="dayto2pe" && (phase==28 || phase==29));
    if (phase != 16 && phase != 17 && !results) return {};
    const uint32_t object = mem.Read32(game == "daytona2" ? 0x105898 : 0x737a7c);
    if (object >= 0x100000 && object <= 0x7fff00 && !(object & 3))
      rank = uint32_t(mem.Read8(object + 0xc9)) + 1;
  } else if (game == "scud" || game == "scudplus") {
    const unsigned phase = mem.Read8(0x104004);
    // 22/23 finish display; 30/31 are the separate native linked-results path.
    results = phase==22 || phase==23 || phase==30 || phase==31;
    if ((phase < 19 || phase > 21) && !results) return {};
    const uint32_t object = mem.Read32(0x1045a0);
    if (object >= 0x100000 && object <= 0x7fff00 && !(object & 3) && mem.Read32(object + 8) == 0x300)
      rank = uint32_t(mem.Read8(object + 0xd4)) + 1;
    // Do not use results scratch 0x10525c: linked display overwrites it with
    // human-only +d5. +d4 is still the overall/native position including CPUs.
  } else if (game == "srally2") {
    // Both Online Rally entries run this same native ROM. Exported TX header:
    // modes +08/+0c, race phase10, finish/results phases11/12/13.
    const uint32_t phase = mem.Read32(0x4c21e8);
    results = phase==11 || phase==12 || phase==13;
    if (mem.Read32(0x4c21d8) != 1 || mem.Read32(0x4c21dc) != 1 || (phase != 10 && !results)) return {};
    // Native HUD value already includes base rank; do not add another one.
    rank = mem.Read32(0x225a14);
  } else if (game == "lemans24") {
    const uint32_t mode = mem.Read32(0x17e7e0), sub = mem.Read32(0x18d298);
    const uint32_t stage = mem.Read32(0x17f61c);
    // Stage4 is the native post-race/results controller (six substates),
    // not an expansion of the racing Stage3 substate range.
    results = stage==4 && sub<=5;
    if (mem.Read32(0x1604f0) != 0 || mode < 5 || mode > 8 ||
        (!results && (stage != 3 || sub < 2 || sub > 4))) return {};
    // Native overall rank; not the lap/progress/time or human-only counter.
    rank = mem.Read32(0x17fe60);
  } else return {};
  // Native total includes CPU opponents. Only online humans are shown in HUD.
  // A missing rank never hides a verified race/results phase or invents a rank.
  return {!results, rank>=1 && rank<=40 ? rank : 0, results};
}

inline void SortStandings(std::vector<StandingRow>& rows) {
  std::sort(rows.begin(), rows.end(), [](const StandingRow& a, const StandingRow& b) {
    const uint32_t ar = a.rank ? a.rank : 0xffffffffu;
    const uint32_t br = b.rank ? b.rank : 0xffffffffu;
    return ar != br ? ar < br : a.car < b.car;
  });
}
}
#endif
