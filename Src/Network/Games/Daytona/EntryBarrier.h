// Daytona USA (Rev A) adapters for the shared Model 3 race-start ring timing.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_NETWORK_GAMES_DAYTONA_ENTRYBARRIER_H
#define INCLUDED_NETWORK_GAMES_DAYTONA_ENTRYBARRIER_H

#include "Network/Games/RaceStart.h"
#include "Network/Online/Daytona.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <optional>

namespace DaytonaRaceEntry {
// Cabinet IDs are 1 -> N -> ... -> 2 -> 1 on the wire. Index masks and hops
// by the forward ring order so the common helpers need no Model 2 exceptions.
inline unsigned RingIndex(unsigned id, unsigned count) { return id == 1 ? 0 : count + 1 - id; }

// Daytona's entry control is a host-only envelope, never placed in guest RAM.
inline constexpr uint8_t Tag = 0xfd;
inline constexpr std::array<uint8_t, 5> Magic{Tag,'D','T','N','1'};
inline constexpr uint64_t GateTimeoutUs = 60000000, RoundTimeoutUs = 10000000;
struct Control
{
  RaceStart::StartToken token;
};
using Wire = std::array<uint8_t, DaytonaOnline::PayloadSize>;
inline Wire Encode(const Control& c)
{
  Wire wire{};
  std::copy(Magic.begin(), Magic.end(), wire.begin());
  std::memcpy(wire.data()+8, &c.token, sizeof(c.token)); // same LE wire as Model 3
  return wire;
}
inline std::optional<Control> Decode(const uint8_t* p, size_t size)
{
  if (size != DaytonaOnline::PayloadSize || !std::equal(Magic.begin(), Magic.end(), p) ||
      p[6] || p[7]) return {};
  Control c;
  std::memcpy(&c.token, p+8, sizeof(c.token));
  return c;
}

struct Clock { int64_t offset=0, rtt=-1; uint64_t received=0; };

// Same ring-token exchange as Indy, minus the course field. A participant
// marks ready only at its native checkpoint; unrelated cabinets just
// translate and forward tokens without gating their own game.
class Barrier
{
public:
  Barrier() = default;
  Barrier(unsigned index, unsigned machines): m_index(index), m_machines(machines) {}
  bool Active() const { return m_attempt.roster != 0; }
  const char* Error() const { return m_error; }
  uint64_t Entered() const { return m_attempt.entered; }
  uint64_t LastRound() const { return m_attempt.lastRound; }
  uint64_t Lead() const { return RaceStart::ReleaseLead(m_attempt.worstRound,m_attempt.retries); }
  int64_t Deadline() const { return m_attempt.deadline; }
  unsigned Retries() const { return m_attempt.retries; }
  uint32_t Epoch() const { return m_attempt.epoch; }
  uint32_t Serial() const { return m_attempt.serial; }
  bool Committed() const { return m_attempt.committed; }
  bool Released(uint64_t now) const { return !Error() && Committed() && now >= uint64_t(Deadline()); }
  void End() { m_attempt = {}; }

  bool Begin(uint32_t roster, uint64_t now)
  {
    if (Active() || m_machines > DaytonaOnline::Cabinets || !RaceStart::ValidRoster(roster,m_machines,m_index) ||
        (roster & (roster-1)) == 0 || now > uint64_t(INT64_MAX)-GateTimeoutUs)
    {
      Fail("invalid native participants or start checkpoint");
      return false;
    }
    m_attempt.roster=roster; m_attempt.entered=m_attempt.next=now;
    if (Leader()) m_attempt.epoch=++m_leaderEpoch;
    return !Error();
  }
  bool Matches(uint32_t roster) const
  {
    return m_attempt.roster==roster;
  }

  std::optional<Control> Tick(uint64_t now, const Clock& clock)
  {
    if (Error() || !Active()) return {};
    auto& a = m_attempt;
    if (now-a.entered > GateTimeoutUs)
      return Fail("native entry or ring acknowledgement timed out");
    if (a.pending && now-a.sent > RoundTimeoutUs)
    {
      // The kind-1 token did not return via the ring (e.g. dropped during
      // link establishment). Reset and re-send instead of aborting.
      a.pending = 0;
      a.next = now;
      if (++a.retries > RaceStart::MaxLeadRetries)
        return Fail("native entry or ring acknowledgement timed out");
    }
    if (a.pending==3 && now >= uint64_t(a.deadline))
      return Fail("commit did not close before the release deadline");
    if (!Leader() || a.committed || a.pending || now < a.next) return {};
    ++a.serial;
    return Send(1,now,clock);
  }

  std::optional<Control> Receive(Control c, uint64_t now, const Clock& clock)
  {
    if (Error()) return {};
    auto& t = c.token;
    if (m_machines > DaytonaOnline::Cabinets || m_index >= m_machines ||
        t.magic!=RaceStart::StartToken{}.magic || t.reserved || !t.epoch || !t.serial ||
        t.kind<1 || t.kind>3 || (t.kind==1 && t.deadline) ||
        !RaceStart::ValidRoster(t.participants,m_machines,t.origin) ||
        !(t.participants & (t.participants-1)) || t.origin!=RaceStart::Leader(t.participants))
      return Fail("invalid entry control");
    const uint32_t all = (1u<<m_machines)-1, bit = 1u<<m_index;
    const unsigned expectedHops = (m_index+m_machines-t.origin-1)%m_machines;
    const uint32_t upstream = RaceStart::UpstreamMask(t.origin,expectedHops,m_machines);
    if (t.hops!=expectedHops || (t.ready & ~(upstream&t.participants)) || (t.clocks & ~upstream) ||
        t.maxRTT > uint64_t(RaceStart::MaxClockRttUs)) return Fail("invalid entry ring path or clock mask");
    const bool origin = m_index==t.origin, participant = (t.participants&bit)!=0;
    const bool fresh = RaceStart::FreshClock(clock.rtt,clock.received,now);
    const bool ready = participant && Active() && !Committed() && Matches(t.participants) &&
      t.epoch > m_lastAccepted[t.origin];
    auto& a = m_attempt;
    if (origin && (!ready || a.pending!=t.kind || t.epoch!=a.epoch || t.serial!=a.serial))
      return Fail("unexpected entry acknowledgement");
    const bool declined = t.kind==2 && (t.clocks & (upstream&~bit))!=(upstream&~bit);
    if (fresh) { t.clocks|=bit; t.maxRTT=std::max(t.maxRTT,uint64_t(clock.rtt)); }
    else t.clocks&=~bit;
    if (ready) t.ready|=bit; else t.ready&=~bit;
    auto& transit = m_transit[t.origin];
    auto forward = [&]() -> std::optional<Control> { ++t.hops; return c; };
    if (declined) return origin ? Retry(now) : forward();

    if (t.kind!=1)
    {
      auto& p = transit.start;
      const bool same = p.epoch==t.epoch && p.serial==t.serial && p.participants==t.participants;
      const int64_t offset = t.kind==3 ? (origin ? a.offset : p.offset) : clock.offset;
      int64_t local = 0;
      if (!fresh || (participant && !ready) ||
          (!origin && t.kind==3 && (!same || !p.prepared || p.committed)) ||
          !RaceStart::TranslateStartDeadline(t.deadline,offset,now,local))
      {
        if (t.kind==3) return Fail("invalid commit deadline, clock or local readiness");
        if (origin) return Retry(now);
        t.clocks &= ~bit;
        return forward();
      }
      t.deadline = local;
      if (!origin)
      {
        if (t.kind==2)
        {
          if (p.epoch>t.epoch || (p.epoch==t.epoch && p.serial>=t.serial))
            return Fail("duplicate or stale prepare");
          transit = {{t.epoch,t.serial,t.participants,clock.offset,local,true,false}};
        }
        else
        {
          const uint32_t visited=upstream|bit;
          if (!RaceStart::ClocksClose(local,p.deadline) || t.ready!=(t.participants&visited) || t.clocks!=visited)
            return Fail("commit disagrees with prepared clocks or participants");
          p.committed = true;
          m_lastAccepted[t.origin] = t.epoch;
          if (participant)
          {
            a.epoch=t.epoch; a.serial=t.serial; a.deadline=p.deadline; a.committed=true;
          }
        }
        return forward();
      }
      if (t.kind==2) a.offset=clock.offset;
    }
    if (!origin) return forward();

    a.lastRound = now-a.sent;
    a.pending = 0;
    if (t.kind==1)
    {
      if (t.ready!=a.roster || t.clocks!=all) { a.next=now+100000; return {}; }
      a.worstRound=std::max(a.worstRound,a.lastRound);
      const uint64_t lead=RaceStart::ReleaseLead(a.worstRound,a.retries);
      if (lead>RaceStart::MaxLeadUs || now > uint64_t(INT64_MAX)-lead)
        return Fail("network too slow for a bounded entry deadline");
      a.deadline=int64_t(now+lead);
      return Send(2,now,clock,t.maxRTT);
    }
    if (t.ready!=t.participants || t.clocks!=all || !RaceStart::ClocksClose(t.deadline,a.deadline))
      return t.kind==2 ? Retry(now) : Fail("incomplete commit or clock closure");
    if (t.kind==2)
    {
      a.worstRound=std::max(a.worstRound,a.lastRound);
      if (!RaceStart::CommitFits(a.deadline,now,a.lastRound)) return Retry(now);
      return Send(3,now,clock,t.maxRTT);
    }
    m_lastAccepted[t.origin]=t.epoch;
    a.committed=true;
    return {};
  }

private:
  bool Leader() const { return RaceStart::Leader(m_attempt.roster)==m_index; }
  std::optional<Control> Fail(const char* error) { if (!Error()) m_error=error; return {}; }
  std::optional<Control> Retry(uint64_t now)
  {
    auto& a = m_attempt;
    a.worstRound=std::max(a.worstRound,now-a.sent);
    a.pending=0; a.deadline=0; a.next=now+200000;
    if (++a.retries>RaceStart::MaxLeadRetries) return Fail("prepare repeatedly declined");
    return {};
  }
  Control Send(uint32_t kind, uint64_t now, const Clock& clock, uint64_t maxRtt=0)
  {
    auto& a = m_attempt;
    Control c;
    auto& t = c.token;
    t.kind=kind; t.epoch=a.epoch; t.serial=a.serial; t.origin=m_index; t.participants=a.roster;
    t.ready=1u<<m_index;
    t.maxRTT=maxRtt;
    if (RaceStart::FreshClock(clock.rtt,clock.received,now))
    {
      t.clocks=t.ready;
      t.maxRTT=std::max(maxRtt,uint64_t(clock.rtt));
    }
    t.deadline=kind==1 ? 0 : a.deadline;
    a.pending=kind; a.sent=now;
    return c;
  }
  struct Attempt
  {
    uint32_t roster=0, epoch=0, serial=0, pending=0;
    unsigned retries=0;
    uint64_t entered=0, next=0, sent=0, worstRound=0, lastRound=0;
    int64_t deadline=0, offset=0;
    bool committed=false;
  } m_attempt;
  struct Transit
  {
    RaceStart::Transit start;
  };
  std::array<Transit, DaytonaOnline::Cabinets> m_transit{};
  std::array<uint32_t, DaytonaOnline::Cabinets> m_lastAccepted{};
  uint32_t m_leaderEpoch=0;
  unsigned m_index=0, m_machines=0;
  const char* m_error=nullptr;
};
}
#endif
