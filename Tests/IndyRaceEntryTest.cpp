// ROM-free checks for INDY 500's shared start timing and transport adapter.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Network/Online/IndyRaceEntry.h"
#include <cassert>
#include <cstdio>

using namespace IndyRaceEntry;

struct Ring
{
  explicit Ring(unsigned seats): count(seats)
  {
    for (unsigned i=0; i<count; ++i) board[i]=Barrier(i,count);
  }
  unsigned count;
  std::array<Barrier,8> board;
  // Deliberately different monotonic clock epochs, including offsets in both directions.
  std::array<int64_t,8> offsets{100,1900000,-5100000,8700000,7240000,-2430000,12345678,-7654321};
  uint64_t wall=95000000;
  uint64_t Time(unsigned i) const { return uint64_t(int64_t(wall)+offsets[i]); }
  Clock Sample(unsigned i, bool stale=false) const
  {
    return {offsets[(i+count-1)%count]-offsets[i],83000,Time(i)-(stale?3500000:50000)};
  }
  void Begin(uint32_t roster, unsigned course)
  {
    for (unsigned i=0; i<count; ++i) if (roster&(1u<<i)) assert(board[i].Begin(roster,course,Time(i)));
  }
  std::optional<Control> Turn(const Control& sent, unsigned stale=~0u, uint64_t hopUs=12345)
  {
    auto incoming=std::optional<Control>(sent);
    for (unsigned hop=1; hop<=count; ++hop)
    {
      const unsigned index=(sent.token.origin+hop)%count;
      wall+=hopUs;
      assert(incoming && incoming->token.hops==hop-1);
      const auto wire=Encode(*incoming);
      const auto c=Decode(wire.data(),wire.size());
      assert(c);
      incoming=board[index].Receive(*c,Time(index),Sample(index,index==stale));
      if (board[index].Error()) std::fprintf(stderr,"seat %u kind %u: %s\n",index,c->token.kind,board[index].Error());
      assert(!board[index].Error());
    }
    return incoming;
  }
  void Complete(uint32_t mask, uint32_t epoch, unsigned retries=0, uint64_t round=0)
  {
    const unsigned leader=RaceStart::Leader(mask);
    auto& root=board[leader];
    const auto collect=root.Tick(Time(leader),Sample(leader));
    assert(collect && collect->token.kind==1 && collect->token.epoch==epoch);
    const auto prepare=Turn(*collect);
    const uint64_t lead=RaceStart::ReleaseLead(round ? round : 12345*count,retries);
    assert(prepare && prepare->token.kind==2 && prepare->token.participants==mask);
    assert(root.LastRound()==12345*count && root.Lead()==lead);
    assert(prepare->token.deadline==int64_t(Time(leader)+lead));
    const auto commit=Turn(*prepare);
    assert(commit && commit->token.kind==3 && !root.Committed());
    assert(!Turn(*commit));
    for (unsigned i=0; i<count; ++i)
    {
      auto& b=board[i];
      if (!(mask&(1u<<i))) { assert(!b.Active() && !b.Committed()); continue; }
      const int64_t converted=prepare->token.deadline - offsets[leader] + offsets[i];
      assert(b.Committed() && b.Deadline()==converted && b.Epoch()==epoch);
      assert(!b.Released(uint64_t(converted-1)) && b.Released(uint64_t(converted)));
      b.End();
      assert(!b.Released(uint64_t(converted)));
    }
    wall+=lead+1000000;
  }
};

int main()
{
  assert(RingIndex(1,8)==0 && RingIndex(8,8)==1 && RingIndex(2,8)==7);
  assert(RaceStart::ReleaseLead(20000,0)==500000 && RaceStart::ReleaseLead(400000,0)==1500000);
  assert(RaceStart::ReleaseLead(400000,2)==3100000 && RaceStart::ReleaseLead(20000000,0)>RaceStart::MaxLeadUs);
  assert(RaceStart::CommitFits(1200000,1000000,50000) && !RaceStart::CommitFits(1199999,1000000,50000));
  assert(RaceStart::ClocksClose(100,50100) && !RaceStart::ClocksClose(100,50101));
  int64_t translated=0;
  assert(!RaceStart::TranslateStartDeadline(INT64_MAX,INT64_MIN,1,translated));
  assert(!RaceStart::FreshClock(2000001,100,110) && !RaceStart::FreshClock(50,111,110));
  const auto sample=Encode(Control{});
  auto bad=sample;
  bad[1]='G'; assert(!Decode(bad.data(),bad.size())); // old GO cannot be interpreted as entry
  assert(!Decode(sample.data(),sample.size()-1));
  bad=sample; bad[5]=6; assert(!Decode(bad.data(),bad.size()));
  bad=sample; bad[7]=1; assert(!Decode(bad.data(),bad.size()));

  for (unsigned n=2; n<=8; ++n)
  {
    Ring run(n);
    const auto mask=(1u<<n)-1;
    run.Begin(mask,1); run.Complete(mask,1);
    run.Begin(mask,2); run.Complete(mask,2); // same transport session, different native race
  }
  Ring subset(8);
  subset.Begin(0x54,2); subset.Complete(0x54,1); // without Master; five non-participating forwarders
  subset.Begin(0x45,1); subset.Complete(0x45,1);
  subset.Begin(0x56,0); subset.Complete(0x56,1); // third distinct origin
  subset.Begin(0x54,2); subset.Complete(0x54,2);

  Ring unready(4);
  auto& m=unready.board[0];
  assert(m.Begin(0xf,1,unready.Time(0)));
  auto probe=m.Tick(unready.Time(0),unready.Sample(0));
  assert(probe && !unready.Turn(*probe));
  for (unsigned i=1; i<4; ++i) assert(unready.board[i].Begin(0xf,1,unready.Time(i)));
  unready.wall+=100000;
  probe=m.Tick(unready.Time(0),unready.Sample(0));
  assert(probe && !unready.Turn(*probe,2) && !m.Committed()); // stale incoming clock: no deadline
  unready.wall+=100000;
  probe=m.Tick(unready.Time(0),unready.Sample(0));
  assert(probe);
  auto prepare=unready.Turn(*probe);
  assert(prepare && !unready.Turn(*prepare,1) && m.Retries()==1 && !m.Committed());
  unready.wall+=200000;
  unready.Complete(0xf,1,1);

  Ring mismatch(2);
  assert(mismatch.board[0].Begin(3,1,mismatch.Time(0)));
  assert(mismatch.board[1].Begin(3,2,mismatch.Time(1)));
  probe=mismatch.board[0].Tick(mismatch.Time(0),mismatch.Sample(0));
  assert(probe && !mismatch.Turn(*probe) && !mismatch.board[0].Committed());
  mismatch.wall+=GateTimeoutUs+1;
  assert(!mismatch.board[0].Tick(mismatch.Time(0),mismatch.Sample(0)) && mismatch.board[0].Error());

  Ring wrongHop(2);
  wrongHop.Begin(3,0);
  probe=wrongHop.board[0].Tick(wrongHop.Time(0),wrongHop.Sample(0));
  assert(probe); ++probe->token.hops;
  assert(!wrongHop.board[1].Receive(*probe,wrongHop.Time(1),wrongHop.Sample(1)) && wrongHop.board[1].Error());

  Ring missingCommit(2);
  missingCommit.Begin(3,1);
  probe=missingCommit.board[0].Tick(missingCommit.Time(0),missingCommit.Sample(0));
  prepare=missingCommit.Turn(*probe);
  const auto commit=missingCommit.Turn(*prepare);
  assert(commit && !missingCommit.board[0].Committed());
  missingCommit.wall+=500001;
  assert(!missingCommit.board[0].Tick(missingCommit.Time(0),missingCommit.Sample(0)));
  assert(missingCommit.board[0].Error() && !missingCommit.board[0].Released(missingCommit.Time(0)));

  std::puts("PASS Indy entry: 2-8-seat rings, native subsets, repeated epochs, prior GO rejection, clock offsets, readiness, retry and missing/late acknowledgement");
}
