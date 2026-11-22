/**
 ** Supermodel
 ** A Sega Model Series Arcade Emulator.
 **
 ** This file is part of Supermodel.
 **
 ** Supermodel is free software: you can redistribute it and/or modify it under
 ** the terms of the GNU General Public License as published by the Free
 ** Software Foundation, either version 3 of the License, or (at your option)
 ** any later version.
 **
 ** Supermodel is distributed in the hope that it will be useful, but WITHOUT
 ** ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 ** FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 ** more details.
 **
 ** You should have received a copy of the GNU General Public License along
 ** with Supermodel.  If not, see <http://www.gnu.org/licenses/>.
 **/

/*
 * Model2Comm.cpp
 *
 * The Model 2 communication board: MAME's m2comm (M2COMM_SIMULATION), its
 * sockets replaced by CNetBoardLink, or by a loopback when not networked.
 *
 * Based on original work by Ariane Fugmann in MAME. Portions Copyright (c)
 * Ariane Fugmann, used under the BSD 3-Clause License: see Docs/README.txt.
 */

#include "Model2/Model2Comm.h"
#include "Model3/NetBoardLink.h"
#include "Network/Online/Indy500.h"
#include "Network/Online/Srallyc.h"
#include "Network/Online/Daytona.h"
#include "Network/Online/Sgt24h.h"
#include "Network/Games/Indy500/RingWindow.h"
#include "BlockFile.h"
#include "OSD/Logger.h"
#include "OSD/Thread.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdlib>

namespace
{
uint64_t IndyNowUs()
{
  return uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}

uint64_t IndyQueueAge(uint64_t nowUs, uint64_t arrivedUs)
{
  return arrivedUs && nowUs >= arrivedUs ? nowUs - arrivedUs : 0;
}
}

CModel2Comm::CModel2Comm()
{
  memset(m_shared, 0, sizeof(m_shared));
  memset(m_buffer0, 0, sizeof(m_buffer0));
}

CModel2Comm::~CModel2Comm()
{
  m_quit = true;
  if (m_connectThread.joinable())
    m_connectThread.join();
  ClearIndyPlayout();
}

void CModel2Comm::Init(std::shared_ptr<CNetServer> server, const std::string &address, uint16_t port, bool frameSync,
                       const std::string &lobbyTicket, uint64_t relayGuid, bool model2PoseUdp,
                       unsigned expectedSeat, unsigned expectedCount, bool video60)
{
  const bool relay = !lobbyTicket.empty();
  m_frameSync = frameSync && !relay;
  m_indyOnline = relay && relayGuid == Indy500Online::Guid;
  m_srallycOnline = relay && relayGuid == SrallycOnline::Guid;
  m_daytonaOnline = relay && relayGuid == DaytonaOnline::Guid;
  m_sgt24hOnline = relay && relayGuid == Sgt24hOnline::Guid;
  ResetSgt24hFreshness();
  m_framePeriodUs = relay && video60 ? 1000000.0 / 60.0 :
                    m_srallycOnline ? SrallycMotion::FrameUs :
                    m_daytonaOnline ? DaytonaMotion::FrameUs : IndyMotion::FrameUs;
  m_expectedSeat = expectedSeat;
  m_expectedCount = expectedCount;
  if (m_indyOnline)
  {
    // Preserve the proven two-seat pairing settings from the earlier lab.
    // The existing configurable overrides are limited to two-seat labs.
    m_indyDepth = 2;
    m_indyMaxQueueUs = 160000;
    if (const char* env = std::getenv("SUPERMODEL_INDY_EXPERIMENT"))
    {
      char* end = nullptr;
      const long depth = std::strtol(env, &end, 10);
      if (!*end && depth >= 1 && depth <= 16)
      {
        m_indyDepth = (unsigned)depth;
        if (const char* limit = std::getenv("SUPERMODEL_INDY_MAX_QUEUE_MS"))
        {
          const long ms = std::strtol(limit, &end, 10);
          if (!*end && ms > 0 && ms <= 1000)
            m_indyMaxQueueUs = uint64_t(ms) * 1000;
        }
      }
    }
  }
  if (server)
  {
    m_link = std::make_unique<CNetBoardLink>(server, address, port, relay, relay ? relayGuid : 0, lobbyTicket, model2PoseUdp);
    m_connectThread = std::thread(&CModel2Comm::ConnectProc, this);
  }
}

bool CModel2Comm::OnlineSessionFailed() const
{
  return m_link && m_link->IsRelay() && (m_onlineFailed || m_link->RelayIndex() == -2);
}

unsigned CModel2Comm::IndyRelaySeat() const
{
  if (!m_indyOnline || !m_connected || !m_link) return 0;
  const int index = m_link->RelayIndex();
  const unsigned count = m_link->RelayCount();
  return count >= 2 && count <= Indy500Online::Cabinets && index >= 0 && unsigned(index) < count ? unsigned(index+1) : 0;
}

void CModel2Comm::ResetSgt24hFreshness()
{
  std::lock_guard<std::mutex> lock(m_sgt24hFreshnessMutex);
  m_sgt24hCopies={};
  m_sgt24hPoseSender.Clear();
  // Never wrap into a previous epoch. Exhaustion disables observation.
  if (m_sgt24hEpoch==UINT64_MAX) {m_sgt24hFreshness.Clear();return;}
  m_sgt24hFreshness.Begin(++m_sgt24hEpoch);
}

void CModel2Comm::ClearSgt24hFreshness()
{
  std::lock_guard<std::mutex> lock(m_sgt24hFreshnessMutex);
  m_sgt24hCopies={};
  m_sgt24hPoseSender.Clear();
  m_sgt24hFreshness.Clear();
}

void CModel2Comm::ObserveSgt24hApplication(int offset,int size,int received)
{
  if (!m_sgt24hOnline) return;
  std::lock_guard<std::mutex> lock(m_sgt24hFreshnessMutex);
  if (!m_connected || OnlineSessionFailed() || m_linkAlive!=1 || !m_linkEnable ||
      m_linkCount<2 || m_linkCount>Sgt24h::Cabinets || !m_linkID || m_linkID>m_linkCount ||
      offset!=Sgt24h::Rx || size!=Sgt24h::FrameBytes || received!=size+1)
  {
    m_sgt24hFreshness.Clear();
    return;
  }
  m_sgt24hCopies={}; // a newer application invalidates every prior copy witness
  m_sgt24hFreshness.Applied(IndyNowUs());
}

Sgt24h::FrameToken CModel2Comm::Sgt24hAppliedToken() const
{
  if (!m_sgt24hOnline || !m_connected || OnlineSessionFailed() || m_linkAlive!=1 || !m_linkEnable) return {};
  std::lock_guard<std::mutex> lock(m_sgt24hFreshnessMutex);
  return m_sgt24hFreshness.Token();
}

bool CModel2Comm::Sgt24hAppliedCurrent(Sgt24h::FrameToken token,uint64_t now,uint64_t age) const
{
  if (!m_sgt24hOnline || !m_connected || OnlineSessionFailed() || m_linkAlive!=1 || !m_linkEnable) return false;
  std::lock_guard<std::mutex> lock(m_sgt24hFreshnessMutex);
  return m_sgt24hFreshness.Current(token,now,age);
}

void CModel2Comm::ObserveSgt24hRxCopy(uint32_t pc,uint32_t rip,uint32_t source,
                                    uint32_t destination,uint32_t address,uint32_t mask)
{
  if (!m_sgt24hOnline || !m_connected || OnlineSessionFailed() || m_linkAlive!=1 ||
      !m_linkEnable || !m_nativeRoleReady || !m_linkID || m_linkID!=m_nativeSeat) return;
  unsigned cabinet=0;
  if (!Sgt24h::MatchRxCopy(m_linkID-1,m_linkCount,pc,rip,source,destination,address,mask,cabinet)) return;
  std::lock_guard<std::mutex> lock(m_sgt24hFreshnessMutex);
  const auto token=m_sgt24hFreshness.Token();
  if (!token.generation) return;
  // Observe only. Do not call Applied(): repeated guest copies cannot renew age.
  m_sgt24hCopies[cabinet]=token;
}

Sgt24h::FrameToken CModel2Comm::Sgt24hCopyToken(unsigned cabinet,uint64_t now,uint64_t age) const
{
  if (cabinet>=Sgt24h::Cabinets || !m_sgt24hOnline || !m_connected || OnlineSessionFailed() ||
      m_linkAlive!=1 || !m_linkEnable || cabinet>=m_linkCount) return {};
  std::lock_guard<std::mutex> lock(m_sgt24hFreshnessMutex);
  const auto token=m_sgt24hCopies[cabinet];
  return m_sgt24hFreshness.Current(token,now,age)?token:Sgt24h::FrameToken{};
}

void CModel2Comm::LatestSgt24hPose(unsigned cabinet,Sgt24hPose::Received& out)
{
  if (!m_sgt24hOnline || !m_link || !m_link->Sgt24hPoseUdp() || OnlineSessionFailed() ||
      m_linkAlive!=1 || cabinet>=m_linkCount || cabinet+1==m_linkID) {out={};return;}
  CNetBoardLink::UdpSnapshot snap;
  if(!m_link->LatestSnapshot(cabinet,out.stamp,snap))return;
  const auto old=out;out={};out.stamp=snap.sourceUs;
  if(!snap.timed || snap.controlUs || !snap.mask.empty() || !snap.receivedUs ||
     snap.ageUs>Sgt24hPose::FreshUs || snap.uncertaintyUs>Sgt24hPose::UncertaintyUs ||
     !Sgt24hPose::Decode(snap.raw.data(),snap.raw.size(),cabinet,m_linkCount,out.packet)) {out.packet={};return;}
  if(old.packet.epoch==out.packet.epoch && old.packet.n && out.packet.seq<=old.packet.seq){out.packet={};return;}
  out.received=snap.receivedUs;out.age=snap.ageUs;out.uncertainty=snap.uncertaintyUs;
}

unsigned CModel2Comm::NativeRelayCount() const
{
  return m_link && m_connected && !OnlineSessionFailed() ? m_link->RelayCount() : 0;
}

unsigned CModel2Comm::NativeRelaySeat()
{
  if ((!m_srallycOnline && !m_daytonaOnline && !m_sgt24hOnline) || !m_connected || OnlineSessionFailed()) return 0;
  const int index = m_link->RelayIndex();
  const unsigned count = m_link->RelayCount();
  if (index < 0) return 0;
  const unsigned seat = unsigned(index)+1;
  if (count < 2 || count > (m_sgt24hOnline ? Sgt24hOnline::Cabinets : m_daytonaOnline ? DaytonaOnline::Cabinets : SrallycOnline::Cabinets) || seat > count ||
      ((m_expectedSeat || m_expectedCount) && (seat != m_expectedSeat || count != m_expectedCount)))
  {
    FailNativeLink("relay assignment disagrees with the reserved roster");
    return 0;
  }
  m_nativeSeat=seat;
  return seat;
}

unsigned CModel2Comm::DaytonaMotionSeat() const
{
  if (!m_daytonaOnline || m_linkAlive!=1 || OnlineSessionFailed() || m_linkCount<2 ||
      m_linkCount>DaytonaOnline::Cabinets || !m_linkID || m_linkID>m_linkCount || m_shared[0]!=1 ||
      m_shared[2]!=m_linkID || m_shared[3]!=m_linkCount || m_shared[0x12] ||
      m_shared[0x13]!=0x0e || m_shared[0x14]!=0xc0 || m_shared[0x15]!=1) return 0;
  return m_linkID;
}

DaytonaMotion::Row CModel2Comm::DaytonaRow(unsigned offset,unsigned seat,DaytonaMotion::Scene scene) const
{
  using namespace DaytonaMotion;
  const unsigned self=DaytonaMotionSeat();
  if (!self || !seat || seat>m_linkCount) return {};
  if (offset==Tx && seat==self) return DecodeRow(m_shared+Tx,PoseBytes,self,scene);
  if (self==seat || offset!=Rx+((seat+m_linkCount-self-1)%m_linkCount)*RowBytes) return {};
  return DecodeRow(m_shared+offset,PoseBytes,seat,scene);
}

void CModel2Comm::DaytonaRaceScene(DaytonaMotion::Scene scene)
{
  if (scene!=m_daytonaScene) m_daytonaMotionTx.Clear();
  m_daytonaScene=scene;
}

void CModel2Comm::LatestDaytonaPose(unsigned seat,DaytonaMotion::Received& out)
{
  using namespace DaytonaMotion;
  if (!m_link || !m_link->DaytonaPoseUdp() || !seat || seat>m_linkCount) { out={}; return; }
  CNetBoardLink::UdpSnapshot snap;
  if (!m_link->LatestSnapshot(seat-1,out.stamp,snap)) return;
  out={};out.stamp=snap.sourceUs;
  if (snap.timed && !snap.controlUs && snap.mask.empty() &&
      snap.uncertaintyUs<=MaxUncertaintyUs && Decode(snap.raw.data(),snap.raw.size(),seat,out.p))
  {
    out.age=snap.ageUs;out.uncertainty=snap.uncertaintyUs;out.received=snap.receivedUs;
  }
}

unsigned CModel2Comm::SrallycMotionSeat() const
{
  if (!m_srallycOnline || m_linkAlive!=1 || OnlineSessionFailed() || m_linkCount<2 ||
      m_linkCount>SrallycOnline::Cabinets || !m_linkID || m_linkID>m_linkCount || m_shared[0]!=1 ||
      m_shared[2]!=m_linkID || m_shared[3]!=m_linkCount || m_shared[0x12] ||
      m_shared[0x13]!=0x0e || m_shared[0x14]!=0xc0 || m_shared[0x15]!=1) return 0;
  return m_linkID;
}

unsigned CModel2Comm::SrallycEchoOffset() const
{
  using namespace SrallycMotion;
  if (!SrallycMotionSeat() || !m_linkCount) return 0;
  return Rx+(m_linkCount-1)*RowBytes;
}

bool CModel2Comm::SrallycLocalMirror(unsigned offset, unsigned* source) const
{
  using namespace SrallycMotion;
  if (!source || !SrallycMotionSeat() || !m_linkCount) return false;
  const unsigned echo=Rx+(m_linkCount-1)*RowBytes;
  if (offset < echo || offset >= echo+RowBytes) return false;
  *source=Tx+(offset-echo);
  return true;
}

SrallycMotion::Row CModel2Comm::SrallycMotionRow(unsigned offset) const
{
  using namespace SrallycMotion;
  const unsigned self=SrallycMotionSeat();
  if (!self) return {};
  if (offset==Tx) return DecodeRow(m_shared+Tx,HeaderBytes,self);
  if (offset<Rx || (offset-Rx)%RowBytes) return {};
  const unsigned row=(offset-Rx)/RowBytes;
  // The first N rows of RX are the peers in native forward order, then
  // this client's own echo. Ignore the echo and all older history rows.
  if (row+1>=m_linkCount) return {};
  return DecodeRow(m_shared+offset,HeaderBytes,(self+row)%m_linkCount+1);
}

void CModel2Comm::LatestSrallycPose(unsigned seat,SrallycMotion::Received& out)
{
  if (!m_link || !m_link->SrallycPoseUdp() || !seat || seat>SrallycOnline::Cabinets) return;
  CNetBoardLink::UdpSnapshot snap;
  if (!m_link->LatestSnapshot(seat-1,out.stamp,snap)) return;
  const auto previous=out;
  out={};
  out.stamp=snap.sourceUs;
  if (snap.timed && !snap.controlUs && snap.mask.empty() && snap.uncertaintyUs<=SrallycMotion::MaxUncertaintyUs &&
      SrallycMotion::Decode(snap.raw.data(),snap.raw.size(),seat,out.p))
  {
    out.age=snap.ageUs;
    out.uncertainty=snap.uncertaintyUs;
    out.received=snap.receivedUs;
    if (previous.received && previous.p.row.scene==out.p.row.scene && out.p.seq>previous.p.seq && snap.sourceUs>previous.stamp)
    {
      const uint32_t seq=out.p.seq-previous.p.seq;
      const double elapsed=double(snap.sourceUs-previous.stamp)/seq;
      if (seq<=12 && elapsed>=0.7*m_framePeriodUs && elapsed<=100000)
      {
        const double measured=std::max(elapsed,m_framePeriodUs);
        out.periodUs=previous.periodUs>0 ? previous.periodUs*0.8+measured*0.2 : measured;
      }
    }
  }
}

void CModel2Comm::NativeRoleConfigured()
{
  m_nativeRoleReady = true;
  m_nativeLinkDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
}

void CModel2Comm::FailNativeLink(const char* reason)
{
  ClearSgt24hFreshness();
  if (!m_onlineFailed.exchange(true))
    ErrorLog("%s Online: %s.", m_sgt24hOnline ? Sgt24hOnline::Title : m_daytonaOnline ? DaytonaOnline::Title : "Sega Rally Championship", reason);
  m_linkAlive = 2;
  m_linkTimer = 0;
}

// Wait for both sockets, and for the relay's ready messages in an Online room.
void CModel2Comm::ConnectProc()
{
  Thread::SetCurrentName("Model2CommConnect");
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(95);
  while (!m_quit && !OnlineSessionFailed())
  {
    if (m_link->Connect() && m_link->Connected())
    {
      InfoLog("Model 2 communication board: connected.");
      m_connected = true;
      return;
    }
    if (m_link->IsRelay() && std::chrono::steady_clock::now() >= deadline)
    {
      ErrorLog("%s Online: timed out waiting for the relay to start the link.",
               m_sgt24hOnline ? Sgt24hOnline::Title : m_daytonaOnline ? DaytonaOnline::Title : m_srallycOnline ? "Sega Rally Championship" : "INDY 500");
      m_onlineFailed = true;
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
}

// Runs on the emulation thread, once both RLA1 messages agree on the roster,
// before any native packets flow. A direct test ticket has no count until RLA1.
bool CModel2Comm::ConfigureIndyOnline()
{
  m_indyConfigured = true;
  m_indyRaceUdp = m_link->IndyRaceUdp();
  const unsigned count = m_link->RelayCount();
  if (count < 2 || count > Indy500Online::Cabinets) return FailIndyEntry("invalid Online roster");
  if (count >= 2)
  {
    // The native watchdog checks its predecessor's 0,1,2 sequence each frame.
    // Do not publish a burst of several received ring aggregates at once.
    m_indyDepth = 1;
    m_indyMaxQueueUs = 250000;
    InfoLog("INDY 500 Online: %u seats; flow-controlled data/FC ring, local receive age cap=250 ms.", count);
  }
  else
    InfoLog("INDY 500 Online: two-seat playout prime=%u pairs, queue age cap=%.1f ms.", m_indyDepth, m_indyMaxQueueUs / 1000.0);
  return true;
}

bool CModel2Comm::EstablishLink()
{
  if (m_link && m_link->IsRelay())
  {
    const int id = m_link->RelayIndex() + 1;
    const unsigned count = m_link->RelayCount();
    if (id != m_linkID || count != m_linkCount)
    {
      ErrorLog("%s Online: native link ID %u of %u, expected %d of %u. Start a new room.",
               m_sgt24hOnline ? Sgt24hOnline::Title : m_daytonaOnline ? DaytonaOnline::Title : m_srallycOnline ? "Sega Rally Championship" : "INDY 500", unsigned(m_linkID), unsigned(m_linkCount), id, count);
      m_onlineFailed = true;
      m_linkAlive = 2;
      return false;
    }
    if (m_indyOnline && id == 1 && count >= 2)
    {
      // FF took the same ordered path as a returned native data/FC pair.
      // It already includes the slowest hops and guest forwarding cadence.
      const uint64_t roundUs = IndyQueueAge(IndyNowUs(), m_indyFfSentUs);
      m_indySeedLimit = IndyRing::SeedLimit(count, roundUs, uint64_t(m_framePeriodUs));
      InfoLog("INDY 500 Online: %u seats, FF round %.3f ms, startup limit %u pairs.",
              count, roundUs / 1000.0, m_indySeedLimit);
    }
    if (m_indyOnline && !m_indyEntryInitialized)
    {
      m_indyEntry = IndyRaceEntry::Barrier(IndyRaceEntry::RingIndex(m_linkID,count), count);
      m_indyEntryInitialized = true;
    }
    if (m_daytonaOnline && !m_daytonaEntryInitialized)
    {
      m_daytonaEntry = DaytonaRaceEntry::Barrier(DaytonaRaceEntry::RingIndex(m_linkID,count), count);
      m_daytonaEntryInitialized = true;
    }
  }
  InfoLog("Model 2 communication board: link established, id %d of %d.", m_linkID, m_linkCount);
  m_linkAlive = 1;
  m_shared[0] = 0x01;
  m_shared[2] = m_linkID;
  m_shared[3] = m_linkCount;
  return true;
}

// MAME: both sockets are open (always, linked to itself)
bool CModel2Comm::LinesOpen() const
{
  return !m_link || m_connected;
}

void CModel2Comm::Reset()
{
  ResetSgt24hFreshness();
  m_srallycMotionTx.Clear();
  DaytonaRaceScene({});
  m_daytonaFfRetry={};
  if (m_indyEntry.Active()) FailIndyEntry("board reset at the native gate");
  // MAME's device_reset
  m_zfg = 0;
  m_cn = 0;
  m_fg = 0;
  m_linkEnable = 0;
  m_linkAlive = 0;
  m_linkTimer = 0;
  m_zfgDelay = 0;
  m_loopback.clear();
  ClearIndyPlayout();
}

void CModel2Comm::WriteCN(uint8_t data)
{
  ResetSgt24hFreshness();
  if (m_indyEntry.Active()) { FailIndyEntry("native link reset at the gate"); return; }
  ClearIndyPlayout();
  m_cn = data & 1;
  if (!m_cn)
  {
    // reset command
    m_linkEnable = 0;
    m_zfg = 0;
    m_cn = 0;
    m_fg = 0;
  }
  else
  {
    // init command
    m_linkEnable = 1;
    m_linkID = 0;
    m_linkAlive = 0;
    m_linkCount = 0;
    m_linkTimer = 0xe8;   // 58 fps * 4 s
    memset(m_shared, 0, sizeof(m_shared));
    m_shared[0x01] = 0x02;
    // frame size 0E00
    m_shared[0x12] = 0x00;
    m_shared[0x13] = 0x0e;
    // frame offset
    m_shared[0x14] = m_frameOffset & 0xff;
    m_shared[0x15] = m_frameOffset >> 8;
    Tick();
  }
}

uint8_t CModel2Comm::ReadFG()
{
  ReadFGLink();
  return (m_fg | (~m_zfg << 7) | 0x7e) & 0xff;
}

void CModel2Comm::Tick(bool indyRaceContext,unsigned indyMotionSlots)
{
  m_onlineReadBudget=64;
  m_indyGuestViewActive=false;
  if (m_srallycOnline || m_daytonaOnline || m_sgt24hOnline)
  {
    // Wait for the title's native settings to initialize before starting FF/FE.
    if (!m_nativeRoleReady || m_onlineFailed) return;
    if (m_linkAlive == 0 && std::chrono::steady_clock::now() >= m_nativeLinkDeadline)
    {
      FailNativeLink("timed out waiting for native FF/FE link setup");
      return;
    }
  }
  if (m_linkEnable != 1)
    return;
  if (!m_indyConfigured && m_connected && m_indyOnline && !ConfigureIndyOnline())
    return;

  int frameStart = 0x2000;
  int frameSize = std::min((m_shared[0x13] << 8) | m_shared[0x12], 0x1000);
  int frameOffset = (frameStart | (m_shared[0x15] << 8) | m_shared[0x14]) & 0x3fff;
  int dataSize = frameSize + 1;
  int recv = 0;
  int idx = 0;

  // EPR-16726 uses fg for master/slave; EPR-18643(A) checks shared[1], with a fallback to fg
  bool isMaster = m_fg == 1 || m_shared[1] == 1;
  bool isSlave = m_fg == 0 && m_shared[1] == 2;
  bool isRelay = m_fg == 0 && m_shared[1] == 0;
  // Rev A briefly selects an unseated/pass-through mode while checking the
  // board at boot. Never consume or echo a lobby FF until the ROM chooses
  // the actually reserved native role. Do not fabricate FG or shared[1].
  if (m_daytonaOnline)
  {
    const unsigned observed=(unsigned(m_fg)<<8)|m_shared[1];
    if (observed!=m_daytonaObservedRole)
    {
      m_daytonaObservedRole=observed;
      InfoLog("Daytona USA board: slot %u observed FG=%u, mode=%u.",m_nativeSeat,unsigned(m_fg),unsigned(m_shared[1]));
    }
    if (m_nativeSeat==1 ? !isMaster : !isSlave) return;
  }

  if (m_linkAlive == 2)
  {
    // link failed
    m_shared[0] = 0xff;
    return;
  }
  else if (m_linkAlive == 0)
  {
    // link not yet established
    m_shared[0] = 0x00;
    m_shared[2] = 0xff;
    m_shared[3] = 0xff;

    if (LinesOpen())
    {
      m_zfg ^= 1;

      recv = ReadFrame(dataSize);
      while (recv > 0)
      {
        idx = m_buffer0[0];
        if (idx == 0xff)
        {
          // FF: link id
          if (isMaster)
          {
            // the master gets the first id and starts the next state
            m_linkID = 1;
            m_linkCount = m_buffer0[1];
            m_linkTimer = 0;
          }
          else
          {
            // a slave takes its own id, a relay does nothing
            if (isSlave)
              m_buffer0[1]++;
            SendFrame(dataSize);
          }
        }
        else if (idx == 0xfe)
        {
          // FE: link size
          if (isSlave)
          {
            m_linkID = m_buffer0[1];
            m_linkCount = m_buffer0[2];
            m_buffer0[1]--;
            SendFrame(dataSize);
          }
          else if (isRelay)
          {
            m_linkID = 0;
            m_linkCount = m_buffer0[2];
            SendFrame(dataSize);
          }
          if (!EstablishLink()) return;
        }

        recv = m_linkAlive == 0 ? ReadFrame(dataSize) : 0;
      }

      // the master, link not yet established
      if (isMaster && m_linkAlive == 0)
      {
        if (m_linkTimer == 1)
        {
          // Retry the native FF without flooding a slower booting Daytona
          // guest's bounded TCP queue. In-race flow is entirely unchanged.
          if (m_daytonaOnline)
          {
            const auto now=std::chrono::steady_clock::now();
            if (now<m_daytonaFfRetry) return;
            m_daytonaFfRetry=now+std::chrono::milliseconds(250);
          }
          // first packet
          m_buffer0[0] = 0xff;
          m_buffer0[1] = 0x01;
          m_buffer0[2] = 0x00;
          if (m_indyOnline) m_indyFfSentUs = IndyRing::FirstFfStamp(m_indyFfSentUs, IndyNowUs());
          SendFrame(dataSize);
        }
        else if (m_linkTimer == 0)
        {
          // second packet
          m_buffer0[0] = 0xfe;
          m_buffer0[1] = m_linkCount;
          m_buffer0[2] = m_linkCount;
          if (!EstablishLink()) return;
          SendFrame(dataSize);
        }
        else if (m_linkTimer > 1)
          m_linkTimer--;
      }
    }
  }

  if (m_indyDepth && m_linkAlive == 1)
  {
    TickIndyPlayout(frameStart, frameSize, frameOffset, dataSize, isMaster, isSlave, indyRaceContext, indyMotionSlots);
    return;
  }

  // Daytona entry barrier: tick the ring exchange each frame.
  if (m_daytonaOnline && m_linkAlive == 1 && !OnlineSessionFailed() && m_daytonaEntry.Active())
  {
    TickDaytonaEntry();
    if (OnlineSessionFailed()) return;
  }

  // link established
  if (m_linkAlive == 1)
  {
    do
    {
      // waiting for the vsync (frame sync): the read waits
      recv = ReadFrame(dataSize, m_linkTimer == 1);
      while (recv > 0)
      {
        idx = m_buffer0[0];
        if (idx <= m_linkCount)
        {
          // into the ring buffer
          for (int j = 0; j < frameSize; j++)
            m_shared[(frameOffset + j) & 0x3fff] = m_buffer0[1 + j];
          ObserveSgt24hApplication(frameOffset,frameSize,recv);
          m_zfg ^= 1;
          if (isSlave)
            SendData(m_linkID, frameStart, frameSize, dataSize);
          else if (isRelay)
            SendFrame(dataSize);
        }
        else if (idx == 0xfc)
        {
          // FC: vsync
          m_linkTimer = 0;
          if (!isMaster)
            SendFrame(dataSize);
        }
        else if (idx == DaytonaRaceEntry::Tag)
        {
          // Daytona entry control frame
          std::vector<uint8_t> entryFrame(m_buffer0, m_buffer0 + recv);
          if (!ReceiveDaytonaEntry(entryFrame)) return;
        }
        recv = ReadFrame(dataSize);
      }
    } while (m_frameSync && m_linkTimer == 1 && m_linkAlive == 1);

    // wait for the vsync (MAME's -comm_framesync)
    m_linkTimer = m_frameSync ? 1 : 0;

    if (isMaster)
    {
      // the ring buffer
      SendData(m_linkID, frameStart, frameSize, dataSize);
      // vsync
      m_buffer0[0] = 0xfc;
      m_buffer0[1] = 0x01;
      SendFrame(dataSize);
    }

    m_zfgDelay = 2;
  }
}

void CModel2Comm::ClearIndyPlayout()
{
  if (m_indyPrimed || m_indyReceived || m_indyMissed)
    InfoLog("INDY 500 lab playout: received=%u applied=%u empty-vblanks=%u peak-pairs=%zu.",
            m_indyReceived, m_indyApplied, m_indyMissed, m_indyHigh);
  if (m_indyApplied)
    InfoLog("INDY 500 lab receive dwell: n=%u p50_ms=%u p95_ms=%u max_ms=%.3f (excludes transit).",
            m_indyApplied, IndyAgePercentile(50), IndyAgePercentile(95), m_indyPeakAgeUs / 1000.0);
  if (m_indyApplied) m_indyAgeBins.fill(0);
  m_indyPrimed = false;
  m_indySeedPairs = m_indySeedLimit = 0;
  m_indyFfSentUs = 0;
  m_indyFifo.clear();
  m_indyPartial = {};
  m_indyReceived = m_indyApplied = m_indyMissed = 0;
  m_indyHigh = 0;
  m_indyPeakAgeUs = 0;
  m_indyPresentationDwellUs = 0;
  m_indyPresentationHalfRttUs = 0;
  m_indyUdpRace = {};
  if (m_indySplitSent || m_indyNativeUdp)
    InfoLog("INDY 500 race UDP totals: split_pairs=%llu full_pairs=%llu applied_motion=%llu held_motion=%llu tcp_board_bytes=%llu.",
      (unsigned long long)m_indySplitSent,(unsigned long long)m_indyFullSent,(unsigned long long)m_indyNativeUdp,
      (unsigned long long)m_indyNativeHold,(unsigned long long)m_indyWireBytes);
  m_indySplitSent=m_indyFullSent=m_indyNativeUdp=m_indyNativeHold=m_indyWireBytes=0;
  m_indyTcpTxBase.clear(); m_indyTcpRxBase.clear();
  ClearIndyRaceUdp();
}

unsigned CModel2Comm::IndyAgePercentile(unsigned percentile) const
{
  const uint64_t rank = (uint64_t(m_indyApplied) * percentile + 99) / 100;
  uint64_t cumulative = 0;
  for (unsigned i = 0; i < m_indyAgeBins.size(); ++i)
  {
    cumulative += m_indyAgeBins[i];
    if (cumulative >= rank) return i;
  }
  return unsigned(m_indyAgeBins.size()-1);
}

void CModel2Comm::RecordIndyAge(uint64_t ageUs)
{
  ++m_indyAgeBins[std::min(uint64_t(m_indyAgeBins.size()-1), ageUs/1000)];
  m_indyPeakAgeUs = std::max(m_indyPeakAgeUs, ageUs);
}

void CModel2Comm::FailIndyPlayout(const char* reason)
{
  ErrorLog("INDY 500 lab playout: %s", reason);
  m_onlineFailed = true;
  m_linkAlive = 2;
}

bool CModel2Comm::DrainIndyPairs(int dataSize, uint8_t other)
{
  // The host-only race-start token is not board data and cannot affect native pairing.
  if (OnlineSessionFailed()) return false;
  constexpr size_t maxPairs = 48;
  for (size_t budget = 0; budget < 2*maxPairs+8; ++budget)
  {
    std::vector<uint8_t> frame;
    CNetBoardLink::PacketInfo info;
    const auto status = m_link->TryReceive(frame, info);
    if (status == CNetBoardLink::ReceiveResult::Pending) break;
    if (status == CNetBoardLink::ReceiveResult::Closed || (!info.indyPair && frame.size()!=size_t(dataSize)) || !info.receivedUs)
    {
      FailIndyPlayout("closed link, unexpected board size, or missing receive timestamp");
      return false;
    }
    if (info.indyPair)
    {
      if (!m_indyRaceUdp || !m_indyPartial.data.empty() || m_indyFifo.size()==maxPairs)
      {
        FailIndyPlayout("unexpected packed TCP control pair");
        return false;
      }
      IndyPair pair;
      uint8_t fc=0;
      if (!IndyRaceUdp::Unpack(frame,m_linkCount,other,pair.data,pair.holes,fc,&m_indyTcpRxBase))
      {
        FailIndyPlayout("invalid packed TCP control pair");
        return false;
      }
      pair.sync.resize(dataSize);
      pair.sync[0]=0xfc; pair.sync[1]=fc;
      pair.arrivedUs=info.receivedUs;
      m_indyFifo.push_back(std::move(pair));
      m_indyHigh=std::max(m_indyHigh,m_indyFifo.size());
      ++m_indyReceived;
    }
    else if (frame[0] == IndyRaceEntry::Tag)
    {
      if (!ReceiveIndyEntry(frame)) return false;
    }
    else if (frame[0] == DaytonaRaceEntry::Tag)
    {
      if (!ReceiveDaytonaEntry(frame)) return false;
    }
    else if (frame[0] == other)
    {
      if (!m_indyPartial.data.empty())
      {
        FailIndyPlayout("data arrived without previous FC");
        return false;
      }
      m_indyTcpRxBase.clear(); // full native pair ends this delta chain
      m_indyPartial.data = std::move(frame);
      m_indyPartial.arrivedUs = info.receivedUs;
    }
    else if (frame[0] == 0xfc && !m_indyPartial.data.empty())
    {
      if (m_indyFifo.size() == maxPairs)
      {
        FailIndyPlayout("playout queue limit");
        return false;
      }
      m_indyPartial.sync = std::move(frame);
      m_indyFifo.push_back(std::move(m_indyPartial));
      m_indyPartial = {};
      ++m_indyReceived;
      m_indyHigh = std::max(m_indyHigh, m_indyFifo.size());
    }
  }
  return true;
}

uint64_t CModel2Comm::IndyOldestReceived() const
{
  return m_indyFifo.empty() ? m_indyPartial.arrivedUs : m_indyFifo.front().arrivedUs;
}

bool CModel2Comm::CheckIndyAge(uint64_t nowUs, uint64_t receivedUs)
{
  const uint64_t ageUs = IndyQueueAge(nowUs, receivedUs);
  if (!m_indyMaxQueueUs || ageUs <= m_indyMaxQueueUs) return true;
  // Queue dwell is presentation latency, not evidence of a broken link.
  // A short jitter burst or slower cabinet must not fail an otherwise intact
  // ordered stream. DrainIndyPairs retains the hard capacity check; malformed
  // traffic and disconnects still fail. Never skip/replay a native sequence.
  return true;
}

void CModel2Comm::TickIndyPlayout(int frameStart, int frameSize, int frameOffset, int dataSize, bool isMaster, bool isSlave, bool raceContext, unsigned slots)
{
  if (m_linkCount < 2 || m_linkCount > Indy500Online::Cabinets || (!isMaster && !isSlave) || dataSize != int(Indy500Online::PayloadSize))
  {
    FailIndyPlayout("unexpected board mode/size");
    return;
  }
  if (OnlineSessionFailed()) return;
  TickIndyEntry();
  if (m_daytonaOnline) TickDaytonaEntry();
  if (OnlineSessionFailed()) return;
  // In descending ID order, the incoming frame is the predecessor's
  // aggregate (its local record followed by the records it received).
  const uint8_t predecessor = m_linkID == m_linkCount ? 1 : m_linkID + 1;
  if (!DrainIndyPairs(dataSize, predecessor)) return;
  const bool flowControlled = m_linkCount >= 2;
  // Only the master can seed, once per guest frame. With a long ring trip,
  // a fixed seat-count window can expire before its first return packet.
  // Stop extending the window once a complete pair can be consumed.
  const bool seeding = flowControlled && isMaster && IndyRing::ShouldSeed(
      m_linkCount, m_indySeedLimit, m_indySeedPairs, m_indyPrimed, !m_indyFifo.empty());
  if (flowControlled && (isMaster ? !seeding : m_indyPrimed) && m_indyFifo.empty())
  {
    // An empty ring is not a reason to block the emulation thread. Keep
    // the previous guest view and retry next vblank. Do not fabricate a
    // completion, consume a partial pair, or extend the master's credit
    // window: the normal send below runs only when a pair is available.
    ++m_indyMissed;
    m_linkTimer=0;
    m_zfgDelay=2;
    return;
  }
  const uint64_t nowUs = IndyNowUs();
  const uint64_t oldestUs = IndyOldestReceived();
  if (!CheckIndyAge(nowUs, oldestUs)) return;
  if (m_indyRaceUdp)
    PollIndyRaceUdp(m_shared+frameStart,nowUs,raceContext && frameOffset==IndyMotion::Rx,slots);

  if (!seeding && !m_indyPrimed && m_indyFifo.size() >= m_indyDepth)
  {
    m_indyPrimed = true;
    InfoLog("INDY 500 lab playout: primed cabinet %d with %zu pairs.", m_linkID, m_indyFifo.size());
    if (flowControlled && isMaster)
      InfoLog("INDY 500 Online: master primed after sending %u startup pairs.", m_indySeedPairs);
  }
  if (m_indyPrimed && !m_indyFifo.empty())
  {
    auto& pair = m_indyFifo.front();
    if (m_indyRaceUdp)
    {
      if (!ApplyIndyRacePair(pair,frameOffset,nowUs,raceContext && m_indyRaceDrove)) return;
    }
    else for (int j=0;j<frameSize;++j) m_shared[(frameOffset+j)&0x3fff]=pair.data[1+j];
    m_zfg ^= 1;
    if (isSlave) SendIndyPair(frameStart,frameSize,dataSize,&pair.sync,nowUs);
    m_indyFifo.pop_front();
    const uint64_t dwellUs=IndyQueueAge(nowUs, oldestUs);
    RecordIndyAge(dwellUs);
    // A stable playout hint for graphics, not a control input to the queue.
    m_indyPresentationDwellUs += (double(dwellUs)-m_indyPresentationDwellUs)*0.025;
    ++m_indyApplied;
  }
  else if (m_indyPrimed)
    ++m_indyMissed;

  if (isMaster)
  {
    if (seeding) ++m_indySeedPairs;
    SendIndyPair(frameStart,frameSize,dataSize,nullptr,nowUs);
  }
  if (m_indyRaceUdp) PublishIndyRaceMotion(frameOffset,nowUs,raceContext && m_indyRaceDrove);
  m_linkTimer = 0;
  m_zfgDelay = 2;
}

void CModel2Comm::ClearIndyRaceUdp()
{
  m_indyPeers={}; m_indyKnown={}; m_indyVisible={}; m_indyLocalHistory={};
  m_indyAppliedMotion=m_indyWireMotion={}; m_indyGuestViewActive=false; m_indyGuestWriteRows=0;
  m_indyForwardHoles={}; m_indyLocalMotion={}; m_indyLastKeyframe={};
  m_indyReadySince=m_indyLastKeyframeUs=m_indyPauseUdpUntil=0;
  m_indyUnusableSince={};
  m_indyLastUdpUs=0;
  m_indyPrimary=m_indyRaceDrove=false;
}

void CModel2Comm::SetIndyMotionPrimary(bool enabled,uint64_t now)
{
  if (enabled==m_indyPrimary) return;
  m_indyPrimary=enabled;
  InfoLog("INDY 500 race UDP: %s clock_us=%llu.",enabled ? "native movement now UDP primary" : "resuming full TCP motion",
          (unsigned long long)now);
}

void CModel2Comm::PollIndyRaceUdp(const uint8_t* local,uint64_t now,bool raceContext,unsigned slots)
{
  using namespace IndyRaceUdp;
  if (raceContext && slots) m_indyRaceDrove=true;
  // Only a room whose whole roster entered the same native race can split
  // the data. After GO a spin, pit or other special state can send an empty
  // liveness sample; that seat's nonstandard motion remains on TCP.
  const bool eligible=raceContext && m_indyRaceDrove && m_indyUdpRace &&
                      m_indyUdpRace.roster==((1u<<m_linkCount)-1) &&
                      now>=m_indyPauseUdpUntil && m_link->UdpActive();
  m_indyLocalMotion=eligible ? FromRow(m_indyUdpRace,local,m_linkID,m_linkCount,slots) : Sample{};
  bool ready=eligible;
  if (eligible)
  {
    const auto& motion=m_indyLocalMotion;
    const bool newMotion=motion.mask && motion.tick!=m_indyLastUdpTick;
    if (!m_indyLastUdpUs || newMotion || motion.mask!=m_indyLastUdpMask || now-m_indyLastUdpUs>=45000)
    {
      // Even if the ROM holds a tick, don't keep treating the old movement
      // as fresh. The heartbeat only confirms the datagram route.
      auto sample=motion;
      if (m_indyLastUdpUs && !newMotion && motion.mask==m_indyLastUdpMask) sample.mask=0;
      const auto wire=Encode(sample);
      m_link->SendPresentation(wire.data(),wire.size(),now,EnterFreshUs,0,nullptr,0,uint64_t(m_framePeriodUs));
      m_indyLastUdpUs=now;
      m_indyLastUdpTick=motion.tick;
      m_indyLastUdpMask=motion.mask;
    }
    for (unsigned index=0;index<m_linkCount;++index) if (index+1!=m_linkID)
    {
      auto& peer=m_indyPeers[index];
      CNetBoardLink::UdpSnapshot snapshot;
      if (m_link->LatestSnapshot(index,peer.stamp,snapshot))
      {
        peer.stamp=snapshot.sourceUs;
        Sample decoded;
        if (snapshot.timed && !snapshot.controlUs && snapshot.mask.empty() &&
            snapshot.receivedUs>snapshot.ageUs && snapshot.uncertaintyUs<=LeaveFreshUs &&
            Decode(snapshot.raw.data(),snapshot.raw.size(),m_indyUdpRace,index+1,decoded))
        {
          peer.heardUs=snapshot.receivedUs-snapshot.ageUs;
          if (decoded.mask) { peer.sample=decoded; peer.sampleUs=peer.heardUs; }
        }
      }
      ready&=peer.FreshPresence(now,m_indyPrimary?LeaveFreshUs:EnterFreshUs);
    }
    ready&=m_link->UdpPrimaryReady(m_indyPrimary);
  }
  if (!ready)
  {
    m_indyReadySince=0;
    SetIndyMotionPrimary(false,now);
  }
  else if (!m_indyPrimary)
  {
    if (!m_indyReadySince) m_indyReadySince=now;
    if (now-m_indyReadySince>=EnterStableUs) SetIndyMotionPrimary(true,now);
  }
}

bool CModel2Comm::ApplyIndyRacePair(IndyPair& pair,int frameOffset,uint64_t now,bool driving)
{
  using namespace IndyRaceUdp;
  m_indyWireMotion={}; m_indyGuestWriteRows=0;
  m_indyForwardHoles=pair.holes;
  const bool nativeRows=frameOffset==IndyMotion::Rx;
  if ((!nativeRows || !m_indyUdpRace) && std::any_of(pair.holes.begin(),pair.holes.end(),[](uint8_t mask){return mask!=0;}))
  {
    FailIndyPlayout("packed control outside the native receive rows");
    return false;
  }
  // Reconstruct into the consumed packet first, so the guest never sees
  // zeroed positions even if a bad control pair forces the room to end.
  for (unsigned r=0;nativeRows && m_indyUdpRace && r<pair.holes.size();++r)
  {
    auto* row=pair.data.data()+1+r*IndyMotion::RowBytes;
    if (!RacingRow(row,m_linkCount)) continue;
    const unsigned peer=row[0];
    const uint32_t tick=Get(row+IndyMotion::TickOffset,4);
    bool applied=false;
    for (unsigned s=0;s<Slots;++s)
    {
      auto* rec=Record(row,s);
      auto& known=m_indyKnown[peer-1][s];
      if (!(pair.holes[r]&(1u<<s)))
      {
        // Only the current rotation supplies reliable anchors; tail rows
        // can contain older echoes of the same cabinet.
        if (r<m_linkCount)
        {
          const auto raw=ReadMotion(rec);
          // The ROM resets this motion counter at native GO. A full row
          // delivered in TCP order is the anchor even across that reset.
          if (raw) known={raw,tick,0};
        }
        continue;
      }
      const auto state=uint16_t(Get(rec+StateOffset,2));
      if (peer==m_linkID)
      {
        const auto& echo=m_indyLocalHistory[tick%m_indyLocalHistory.size()];
        if (echo.peer==peer && echo.tick==tick && Matches(echo,s,state,tick))
        {
          std::copy_n(echo.cars[s].bytes.data(),MotionBytes,rec);
          continue;
        }
        FailIndyPlayout("packed control lost an exact local echo");
        return false;
      }
      else if (driving && r<m_linkCount)
      {
        const auto& remote=m_indyPeers[peer-1];
        if (remote.FreshMotion(now,LeaveFreshUs) && Matches(remote.sample,s,state,tick) && Continuous(known,remote.sample,s))
        {
          known={remote.sample.cars[s],remote.sample.tick,remote.sampleUs};
          applied=true;
          ++m_indyNativeUdp;
        }
        else ++m_indyNativeHold;
      }
      // No extrapolated or zero positions go into CommRAM. If a datagram was
      // lost, use the last native sample until that owner's TCP motion resumes.
      if (!known.motion)
      {
        FailIndyPlayout("packed control arrived without a motion anchor");
        return false;
      }
      std::copy_n(known.motion.bytes.data(),MotionBytes,rec);
      if (r<m_linkCount && peer!=m_linkID && known.sampleUs)
        m_indyWireMotion[r][s]={known.tick,known.sampleUs};
    }
    if (r<m_linkCount && peer!=m_linkID)
    {
      auto& since=m_indyUnusableSince[peer-1];
      if (!driving || !pair.holes[r] || applied || !m_indyPeers[peer-1].FreshMotion(now,EnterFreshUs)) since=0;
      else if (!since) since=now;
      else if (now-since>=240000 && now>=m_indyPauseUdpUntil)
      {
        // A healthy socket is not proof that the samples fit the ROM's
        // current rows. Withdraw consent so the room restores full anchors.
        m_indyPauseUdpUntil=now+1100000;
        m_indyReadySince=0;
        InfoLog("INDY 500 race UDP: pausing unusable native samples to refresh reliable anchors.");
        SetIndyMotionPrimary(false,now);
      }
    }
  }
  for (unsigned j=0;j+1<pair.data.size();++j) m_shared[(frameOffset+j)&0x3fff]=pair.data[1+j];
  return true;
}

void CModel2Comm::PublishIndyRaceMotion(int frameOffset,uint64_t now,bool driving)
{
  using namespace IndyRaceUdp;
  m_indyAppliedMotion=m_indyWireMotion;
  if (!driving || frameOffset!=IndyMotion::Rx || !m_indyPrimed || !m_indyApplied)
  {
    m_indyVisible={};
    return;
  }
  for (unsigned r=0;r<m_linkCount;++r)
  {
    const unsigned peer=1+(unsigned(m_linkID)+r)%m_linkCount;
    if (peer==m_linkID) continue;
    auto& visible=m_indyVisible[peer-1];
    const auto* row=m_shared+frameOffset+r*IndyMotion::RowBytes;
    if ((m_indyGuestWriteRows&(1u<<r)) || row[0]!=peer || !RacingRow(row,m_linkCount)) { visible={}; continue; }
    const uint32_t controlTick=Get(row+IndyMotion::TickOffset,4);
    const auto& remote=m_indyPeers[peer-1];
    for (unsigned s=0;s<Slots;++s)
    {
      const auto native=ReadMotion(Record(row,s));
      if (!native) { visible[s]={}; continue; }
      const auto source=m_indyWireMotion[r][s];
      const Known ordered{native,source.sampleUs ? source.tick : controlTick,source.sampleUs};
      Known udp;
      if ((m_indyPrimary || (m_indyForwardHoles[r]&(1u<<s))) && remote.FreshMotion(now,LeaveFreshUs) &&
          Matches(remote.sample,s,native.state,controlTick))
        udp={remote.sample.cars[s],remote.sample.tick,remote.sampleUs};
      const auto selected=VisibleMotion(ordered,visible[s],udp,controlTick,now);
      visible[s]=selected;
      m_indyAppliedMotion[r][s]={selected.tick,selected.sampleUs};
      // Always mirror the selected motion into the guest view. Skipping the
      // write when it equals the native row left a stale overlay value on
      // screen once UDP corrections stopped, freezing the car.
      if (!m_indyGuestViewActive)
      {
        std::copy_n(m_shared+IndyMotion::Rx,m_indyGuestView.size(),m_indyGuestView.data());
        m_indyGuestViewActive=true;
      }
      auto* guestRow=m_indyGuestView.data()+r*IndyMotion::RowBytes;
      std::copy_n(selected.motion.bytes.data(),MotionBytes,Record(guestRow,s));
    }
  }
}

void CModel2Comm::SendIndyPair(int frameStart,int frameSize,int dataSize,const std::vector<uint8_t>* sync,uint64_t now)
{
  using namespace IndyRaceUdp;
  if (m_indyRaceUdp)
  {
    m_buffer0[0]=m_linkID;
    std::copy_n(m_shared+frameStart,frameSize,m_buffer0+1);
    const auto fc=sync ? (*sync)[1] : uint8_t(1);
    Holes holes{};
    const auto& local=m_indyLocalMotion;
    if (local.mask)
    {
      auto& echo=m_indyLocalHistory[local.tick%m_indyLocalHistory.size()];
      const bool exact=echo.peer!=local.peer || echo.tick!=local.tick || SameMotion(echo,local);
      // A full TCP anchor on entry, on car-state change, and periodically.
      // All other racing position samples travel via UDP, not on each hop.
      if (exact && m_indyPrimary && m_indyLastKeyframeUs && now-m_indyLastKeyframeUs<KeyframeUs &&
          local.mask==m_indyLastKeyframe.mask)
      {
        bool same=true;
        for (unsigned s=0;s<Slots;++s) if (local.mask&(1u<<s))
          same&=local.cars[s].state==m_indyLastKeyframe.cars[s].state;
        if (same) { holes[0]=local.mask; echo=local; }
      }
      if (!holes[0]) { m_indyLastKeyframe=local; m_indyLastKeyframeUs=now; }
    }
    // Shift the incoming footprint along with the native rows. An exact UDP
    // coordinate restored for the local guest is NOT duplicated onto TCP at
    // the next hop. On fallback each owner immediately fills its own row;
    // previously stripped rows fall out of this bounded shift register.
    for (unsigned r=1;r<holes.size();++r)
    {
      const auto* row=m_buffer0+1+r*IndyMotion::RowBytes;
      if (RacingRow(row,m_linkCount) && row[0]==1+(unsigned(m_linkID)-1+r)%m_linkCount)
      {
        holes[r]=m_indyForwardHoles[r-1];
        for (unsigned s=0;s<Slots;++s) if ((holes[r]&(1u<<s)) && !(ReadMotion(Record(row,s))))
          holes[r]&=uint8_t(~(1u<<s));
      }
    }
    if (fc==1 && std::any_of(holes.begin(),holes.end(),[](uint8_t mask){return mask!=0;}))
    {
      const auto packed=Pack(m_buffer0,holes,fc,&m_indyTcpTxBase);
      if (!packed.empty())
      {
        m_link->SendIndyRacePair(packed.data(),packed.size());
        ++m_indySplitSent;
        m_indyWireBytes+=packed.size()+8;
        return;
      }
    }
    m_indyTcpTxBase.clear();
    ++m_indyFullSent;
    m_indyWireBytes+=2*(dataSize+8);
  }
  SendData(m_linkID,frameStart,frameSize,dataSize);
  if (sync) std::copy_n(sync->data(),dataSize,m_buffer0);
  else { m_buffer0[0]=0xfc; m_buffer0[1]=1; }
  SendFrame(dataSize);
}

bool CModel2Comm::FailIndyEntry(const char* reason)
{
  if (!m_onlineFailed) ErrorLog("INDY 500 ENTRY ABORT: %s; no unsynchronized fallback.", reason);
  m_onlineFailed = true;
  m_linkAlive = 2;
  return false;
}

bool CModel2Comm::FailDaytonaEntry(const char* reason)
{
  if (!m_onlineFailed) ErrorLog("DAYTONA USA ENTRY ABORT: %s; no unsynchronized fallback.", reason);
  m_onlineFailed = true;
  m_linkAlive = 2;
  return false;
}

void CModel2Comm::SendIndyEntry(const IndyRaceEntry::Control& control)
{
  const auto& t = control.token;
  if (t.kind == 2 && t.hops == 0)
    InfoLog("INDY 500 ENTRY PREPARE seat=%u epoch=%u serial=%u round_us=%llu lead_us=%llu retries=%u roster=%02x",
            unsigned(m_linkID), t.epoch, t.serial, (unsigned long long)m_indyEntry.LastRound(),
            (unsigned long long)m_indyEntry.Lead(), m_indyEntry.Retries(), t.participants);
  const auto wire = IndyRaceEntry::Encode(control);
  m_link->Send(wire.data(), wire.size());
}

void CModel2Comm::SendDaytonaEntry(const DaytonaRaceEntry::Control& control)
{
  const auto& t = control.token;
  if (t.kind == 2 && t.hops == 0)
    InfoLog("DAYTONA USA ENTRY PREPARE seat=%u epoch=%u serial=%u round_us=%llu lead_us=%llu retries=%u roster=%02x",
            unsigned(m_linkID), t.epoch, t.serial, (unsigned long long)m_daytonaEntry.LastRound(),
            (unsigned long long)m_daytonaEntry.Lead(), m_daytonaEntry.Retries(), t.participants);
  const auto wire = DaytonaRaceEntry::Encode(control);
  m_link->Send(wire.data(), wire.size());
}

void CModel2Comm::TickIndyEntry()
{
  const uint64_t now = IndyNowUs();
  const auto clock = m_link->GetIncomingClock();
  m_indyPresentationHalfRttUs = RaceStart::FreshClock(clock.rtt,clock.received,now) ? uint64_t(clock.rtt)/2 : 0;
  const uint64_t interval = m_indyEntry.Active() ? std::max<uint64_t>(300000, std::max<int64_t>(0,clock.rtt)) : 1000000;
  if (now - m_indyLastPingUs >= interval)
  {
    m_indyLastPingUs = now;
    m_link->RefreshIncomingClock();
  }
  if (const auto next = m_indyEntry.Tick(now, {clock.offset,clock.rtt,clock.received})) SendIndyEntry(*next);
  if (m_indyEntry.Error()) FailIndyEntry(m_indyEntry.Error());
}

void CModel2Comm::TickDaytonaEntry()
{
  const uint64_t now = IndyNowUs();
  const auto clock = m_link->GetIncomingClock();
  const uint64_t interval = m_daytonaEntry.Active() ? std::max<uint64_t>(300000, std::max<int64_t>(0,clock.rtt)) : 1000000;
  if (now - m_daytonaLastPingUs >= interval)
  {
    m_daytonaLastPingUs = now;
    m_link->RefreshIncomingClock();
  }
  if (const auto next = m_daytonaEntry.Tick(now, {clock.offset,clock.rtt,clock.received})) SendDaytonaEntry(*next);
  if (m_daytonaEntry.Error()) FailDaytonaEntry(m_daytonaEntry.Error());
}

bool CModel2Comm::ReceiveIndyEntry(const std::vector<uint8_t>& frame)
{
  const auto control = IndyRaceEntry::Decode(frame.data(), frame.size());
  if (!control) return FailIndyEntry("invalid or incompatible entry control version");
  const auto clock = m_link->GetIncomingClock();
  const uint64_t now = IndyNowUs();
  const unsigned retries = m_indyEntry.Retries();
  const bool committed = m_indyEntry.Committed();
  const auto next = m_indyEntry.Receive(*control,now,{clock.offset,clock.rtt,clock.received});
  if (m_indyEntry.Error()) return FailIndyEntry(m_indyEntry.Error());
  if (m_indyEntry.Retries() != retries)
  {
    InfoLog("INDY 500 ENTRY RETRY seat=%u epoch=%u serial=%u retries=%u", unsigned(m_linkID),
            m_indyEntry.Epoch(), m_indyEntry.Serial(), m_indyEntry.Retries());
    m_link->RefreshIncomingClock();
  }
  if (next) SendIndyEntry(*next);
  if (!committed && m_indyEntry.Committed())
    InfoLog("INDY 500 ENTRY COMMITTED seat=%u epoch=%u serial=%u clock_us=%llu deadline_us=%llu",
            unsigned(m_linkID), m_indyEntry.Epoch(), m_indyEntry.Serial(),
            (unsigned long long)now, (unsigned long long)m_indyEntry.Deadline());
  return true;
}

bool CModel2Comm::ReceiveDaytonaEntry(const std::vector<uint8_t>& frame)
{
  const auto control = DaytonaRaceEntry::Decode(frame.data(), frame.size());
  if (!control) return FailDaytonaEntry("invalid or incompatible entry control version");
  const auto clock = m_link->GetIncomingClock();
  const uint64_t now = IndyNowUs();
  const unsigned retries = m_daytonaEntry.Retries();
  const bool committed = m_daytonaEntry.Committed();
  const auto next = m_daytonaEntry.Receive(*control,now,{clock.offset,clock.rtt,clock.received});
  if (m_daytonaEntry.Error()) return FailDaytonaEntry(m_daytonaEntry.Error());
  if (m_daytonaEntry.Retries() != retries)
  {
    InfoLog("DAYTONA USA ENTRY RETRY seat=%u epoch=%u serial=%u retries=%u", unsigned(m_linkID),
            m_daytonaEntry.Epoch(), m_daytonaEntry.Serial(), m_daytonaEntry.Retries());
    m_link->RefreshIncomingClock();
  }
  if (next) SendDaytonaEntry(*next);
  if (!committed && m_daytonaEntry.Committed())
    InfoLog("DAYTONA USA ENTRY COMMITTED seat=%u epoch=%u serial=%u clock_us=%llu deadline_us=%llu",
            unsigned(m_linkID), m_daytonaEntry.Epoch(), m_daytonaEntry.Serial(),
            (unsigned long long)now, (unsigned long long)m_daytonaEntry.Deadline());
  return true;
}

bool CModel2Comm::IndyUdpActive() const
{
  return m_indyOnline && !m_indyRaceUdp && m_link && m_link->UdpActive();
}

void CModel2Comm::SendIndyPose(const IndyPoseUdp::Bytes& bytes,uint64_t now)
{
  // No UDP delta bases or control labels. Only a full, disposable pose
  // bundle. TX may have been written during the preceding guest frame.
  m_link->SendPresentation(bytes.data(),bytes.size(),now,IndyPoseUdp::MaxAgeUs,0,nullptr,0,
                           uint64_t(m_framePeriodUs));
}

bool CModel2Comm::LatestIndyPose(unsigned peer,IndyPoseUdp::Received& out)
{
  CNetBoardLink::UdpSnapshot snap;
  // The verified native cabinet ID is the relay's zero-based origin + 1.
  if (!peer || peer>m_linkCount || peer==m_linkID || !m_link->LatestSnapshot(peer-1,out.stamp,snap)) return false;
  out = {};
  out.stamp=snap.sourceUs; out.receivedUs=snap.receivedUs; out.ageUs=snap.ageUs;
  out.timed=snap.timed && !snap.controlUs && snap.mask.empty() && snap.uncertaintyUs<=IndyPoseUdp::MaxAgeUs &&
    IndyPoseUdp::Decode(snap.raw.data(),snap.raw.size(),m_indyUdpRace,peer,out.packet);
  return out.timed;
}

CModel2Comm::Entry CModel2Comm::GateIndyEntry(uint32_t roster, unsigned course, uint64_t frame)
{
  if (OnlineSessionFailed()) return Entry::Failed;
  const unsigned index = IndyRaceEntry::RingIndex(m_linkID,m_linkCount);
  if (!m_indyOnline || !m_indyEntryInitialized || m_linkAlive != 1)
  {
    FailIndyEntry("native link was not established at race entry");
    return Entry::Failed;
  }
  if (course > IndyRaceEntry::MaxCourse || !RaceStart::ValidRoster(roster,m_linkCount,index))
  {
    FailIndyEntry("native gate without a verified race roster");
    return Entry::Failed;
  }
  m_indyUdpRace = {};
  if ((roster & (roster-1)) == 0 && !m_indyEntry.Active()) return Entry::Release; // native solo race

  const uint64_t now = IndyNowUs();
  if (!m_indyEntry.Active())
  {
    if (!m_indyEntry.Begin(roster,course,now))
    {
      FailIndyEntry(m_indyEntry.Error());
      return Entry::Failed;
    }
    InfoLog("INDY 500 ENTRY READY seat=%u frame=%llu clock_us=%llu course=%u roster=%02x (native flag screen)",
            unsigned(m_linkID), (unsigned long long)frame, (unsigned long long)now, course, roster);
  }
  else if (!m_indyEntry.Matches(roster,course))
  {
    FailIndyEntry("native cohort changed at the gate");
    return Entry::Failed;
  }
  if (!m_indyEntry.Released(now)) return Entry::Wait;

  InfoLog("INDY 500 ENTRY RELEASE seat=%u epoch=%u serial=%u frame=%llu clock_us=%llu deadline_us=%llu held_us=%llu",
          unsigned(m_linkID), m_indyEntry.Epoch(), m_indyEntry.Serial(), (unsigned long long)frame,
          (unsigned long long)now, (unsigned long long)m_indyEntry.Deadline(),
          (unsigned long long)(now-m_indyEntry.Entered()));
  m_indyUdpRace = {m_indyEntry.Epoch(),m_indyEntry.Serial(),uint8_t(roster),uint8_t(course)};
  if (m_indyRaceUdp) ClearIndyRaceUdp();
  m_indyEntry.End();
  return Entry::Release;
}

CModel2Comm::Entry CModel2Comm::GateDaytonaEntry(uint32_t roster, uint64_t frame)
{
  if (OnlineSessionFailed()) return Entry::Failed;
  const unsigned index = DaytonaRaceEntry::RingIndex(m_linkID,m_linkCount);
  if (!m_daytonaOnline || !m_daytonaEntryInitialized || m_linkAlive != 1)
  {
    FailDaytonaEntry("native link was not established at race entry");
    return Entry::Failed;
  }
  if (!RaceStart::ValidRoster(roster,m_linkCount,index))
  {
    FailDaytonaEntry("native gate without a verified race roster");
    return Entry::Failed;
  }
  if ((roster & (roster-1)) == 0 && !m_daytonaEntry.Active()) return Entry::Release; // native solo race

  const uint64_t now = IndyNowUs();
  if (!m_daytonaEntry.Active())
  {
    if (!m_daytonaEntry.Begin(roster,now))
    {
      FailDaytonaEntry(m_daytonaEntry.Error());
      return Entry::Failed;
    }
    InfoLog("DAYTONA USA ENTRY READY seat=%u frame=%llu clock_us=%llu roster=%02x (native transmission choose)",
            unsigned(m_linkID), (unsigned long long)frame, (unsigned long long)now, roster);
  }
  else if (!m_daytonaEntry.Matches(roster))
  {
    FailDaytonaEntry("native cohort changed at the gate");
    return Entry::Failed;
  }
  if (!m_daytonaEntry.Released(now)) return Entry::Wait;

  InfoLog("DAYTONA USA ENTRY RELEASE seat=%u epoch=%u serial=%u frame=%llu clock_us=%llu deadline_us=%llu held_us=%llu",
          unsigned(m_linkID), m_daytonaEntry.Epoch(), m_daytonaEntry.Serial(), (unsigned long long)frame,
          (unsigned long long)now, (unsigned long long)m_daytonaEntry.Deadline(),
          (unsigned long long)(now-m_daytonaEntry.Entered()));
  m_daytonaEntry.End();
  return Entry::Release;
}

// MAME's read_fg: frames that arrived since the vblank
void CModel2Comm::ReadFGLink()
{
  // Once linked, this Indy-only experiment publishes one intact pair at vblank.
  if (m_indyDepth && m_linkAlive == 1) return;
  if (m_zfgDelay > 0)
  {
    m_zfgDelay--;
    return;
  }
  if (m_linkAlive != 1)
    return;

  int frameStart = 0x2000;
  int frameSize = std::min((m_shared[0x13] << 8) | m_shared[0x12], 0x1000);
  int frameOffset = (frameStart | (m_shared[0x15] << 8) | m_shared[0x14]) & 0x3fff;
  int dataSize = frameSize + 1;

  bool isMaster = m_fg == 1 || m_shared[1] == 1;
  bool isSlave = m_fg == 0 && m_shared[1] == 2;
  bool isRelay = m_fg == 0 && m_shared[1] == 0;

  do
  {
    int recv = ReadFrame(dataSize, m_linkTimer == 1);
    while (recv > 0)
    {
      int idx = m_buffer0[0];
      if (idx <= m_linkCount)
      {
        for (int j = 0; j < frameSize; j++)
          m_shared[(frameOffset + j) & 0x3fff] = m_buffer0[1 + j];
          ObserveSgt24hApplication(frameOffset,frameSize,recv);
        m_zfg ^= 1;
        if (isSlave)
          SendData(m_linkID, frameStart, frameSize, dataSize);
        else if (isRelay)
          SendFrame(dataSize);
      }
      else if (idx == 0xfc)
      {
        m_linkTimer = 0;
        if (!isMaster)
          SendFrame(dataSize);
      }
      recv = ReadFrame(dataSize);
    }
  } while (m_frameSync && m_linkTimer == 1 && m_linkAlive == 1);
}

// One frame into m_buffer0: its size, or 0 if there is none (wait: until one
// arrives or the link breaks)
int CModel2Comm::ReadFrame(int dataSize, bool wait)
{
  if (!m_link)
  {
    if (m_loopback.empty())
      return 0;
    std::vector<uint8_t> frame = std::move(m_loopback.front());
    m_loopback.pop_front();
    memcpy(m_buffer0, frame.data(), std::min<size_t>(frame.size(), sizeof(m_buffer0)));
    return (int) frame.size();
  }
  if (!m_connected) return 0;
  std::vector<uint8_t> polled;
  if (m_link->IsRelay())
  {
    // Online reads must never enter Receive's condition-variable wait,
    // including after loading a state containing a legacy sync timer.
    if(m_linkAlive==1) m_linkTimer=0; // preserve FF/FE startup countdown
    // Bound receive work across vblank and repeated guest FG polls.
    // Excess packets remain queued in order for the next emulated frame.
    if(!m_onlineReadBudget) return 0;
    const auto result=m_link->TryReceive(polled);
    if(result==CNetBoardLink::ReceiveResult::Data) --m_onlineReadBudget;
    if(result==CNetBoardLink::ReceiveResult::Pending) return 0;
  }
  else if (!wait && !m_link->CheckDataAvailable()) return 0;
  std::vector<uint8_t> &frame = m_link->IsRelay() ? polled : m_link->Receive();
  if (frame.empty())
  {
    ClearSgt24hFreshness();
    // the previous machine has gone
    if (m_linkAlive == 1)
    {
      ErrorLog("Model 2 communication board: connection lost.");
      m_linkAlive = 2;
      m_linkTimer = 0;
    }
    return 0;
  }
  if (m_link->IsRelay() && frame.size() != size_t(dataSize) &&
      !(m_daytonaOnline && frame[0] == DaytonaRaceEntry::Tag))
  {
    ClearSgt24hFreshness();
    ErrorLog("Model 2 Online: local and relayed board frame sizes disagree.");
    m_onlineFailed = true;
    m_linkAlive = 2;
    return 0;
  }
  if (m_indyOnline && frame[0] == IndyRaceEntry::Tag)
  {
    FailIndyEntry("entry control before native link was ready");
    return 0;
  }
  if (m_daytonaOnline && m_linkAlive != 1 && frame[0] == DaytonaRaceEntry::Tag)
  {
    // Entry control arrived during link establishment. The sender
    // reached its checkpoint before our link was ready. Skip it; the
    // barrier will re-send and DrainIndyPairs will receive it later.
    return 0;
  }
  memcpy(m_buffer0, frame.data(), std::min<size_t>(frame.size(), sizeof(m_buffer0)));
  return (int) frame.size();
}

void CModel2Comm::SendData(uint8_t frameType, int frameStart, int frameSize, int dataSize)
{
  m_buffer0[0] = frameType;
  for (int i = 0; i < frameSize; i++)
    m_buffer0[1 + i] = m_shared[(frameStart + i) & 0x3fff];
  SendFrame(dataSize);
  if (m_sgt24hOnline && m_link && m_link->Sgt24hPoseUdp() && !OnlineSessionFailed())
  {
    if (m_linkID && m_linkID==m_nativeSeat && frameType==m_linkID && frameStart==Sgt24h::Tx &&
        frameSize==Sgt24h::FrameBytes && dataSize==Sgt24hOnline::PayloadSize)
    {
      Sgt24hPose::Wire wire{};bool send=false;
      {
        std::lock_guard<std::mutex> lock(m_sgt24hFreshnessMutex);
        if (const auto* packet=m_sgt24hPoseSender.Push(m_buffer0+1,Sgt24h::RowBytes,m_linkID-1,m_linkCount,m_sgt24hEpoch))
        {wire=Sgt24hPose::Encode(*packet);send=true;}
      }
      if(send)m_link->SendPresentation(wire.data(),wire.size(),IndyNowUs(),Sgt24hPose::FreshUs,0,nullptr,0,uint64_t(m_framePeriodUs));
    }
  }
  // The complete TCP frame was already offered on the normal path. UDP
  // can echo a pose from the exact local TX row but never omits any bytes.
  if (m_daytonaOnline && m_daytonaScene && m_link && m_link->DaytonaPoseUdp())
  {
    const unsigned self=DaytonaMotionSeat();
    const auto row=(self && self==frameType && frameStart==DaytonaMotion::Tx && dataSize==DaytonaOnline::PayloadSize) ?
      DaytonaMotion::DecodeRow(m_buffer0+1,unsigned(frameSize),self,m_daytonaScene) : DaytonaMotion::Row{};
    if (const auto* p=m_daytonaMotionTx.Push(row))
    {
      const auto wire=DaytonaMotion::Encode(*p);
      m_link->SendPresentation(wire.data(),wire.size(),IndyNowUs(),DaytonaMotion::FreshUs,0,nullptr,0,uint64_t(m_framePeriodUs));
    }
  }
  if (m_srallycOnline && m_link && m_link->SrallycPoseUdp())
  {
    const unsigned self=SrallycMotionSeat();
    const auto row=(self && self==frameType && frameStart==SrallycMotion::Tx && dataSize==SrallycOnline::PayloadSize) ?
      SrallycMotion::DecodeRow(m_buffer0+1,unsigned(frameSize),self) : SrallycMotion::Row{};
    if (const auto* pose=m_srallycMotionTx.Push(row))
    {
      const auto wire=SrallycMotion::Encode(*pose);
      m_link->SendPresentation(wire.data(),wire.size(),IndyNowUs(),SrallycMotion::MaxUdpAgeUs,0,nullptr,0,
                               uint64_t(m_framePeriodUs));
    }
  }
}

void CModel2Comm::SendFrame(int dataSize)
{
  if (!m_link)
  {
    m_loopback.emplace_back(m_buffer0, m_buffer0 + dataSize);
    return;
  }
  if (m_connected)
    m_link->Send(m_buffer0, dataSize);
}

void CModel2Comm::SaveState(CBlockFile *file)
{
  file->NewBlock("Model 2 comm", __FILE__);
  file->Write(m_shared, sizeof(m_shared));
  file->Write(m_zfg);
  file->Write(m_cn);
  file->Write(m_fg);
  file->Write(m_linkEnable);
  file->Write(m_linkTimer);
  file->Write(m_linkAlive);
  file->Write(m_linkID);
  file->Write(m_linkCount);
  file->Write(m_zfgDelay);
  // the loopback frames (not the network's)
  uint32_t n = (uint32_t) m_loopback.size();
  file->Write(n);
  for (const auto &frame : m_loopback)
  {
    uint32_t size = (uint32_t) frame.size();
    file->Write(size);
    file->Write(frame.data(), size);
  }
}

void CModel2Comm::LoadState(CBlockFile *file)
{
  ResetSgt24hFreshness();
  m_srallycMotionTx.Clear();
  if (Result::OKAY != file->FindBlock("Model 2 comm"))
  {
    ErrorLog("Unable to load Model 2 communication board state. Save state file is corrupt.");
    return;
  }
  m_indyGuestViewActive=false;
  m_indyWireMotion=m_indyAppliedMotion={}; m_indyVisible={}; m_indyGuestWriteRows=0;
  file->Read(m_shared, sizeof(m_shared));
  file->Read(m_zfg);
  file->Read(m_cn);
  file->Read(m_fg);
  file->Read(m_linkEnable);
  file->Read(m_linkTimer);
  file->Read(m_linkAlive);
  file->Read(m_linkID);
  file->Read(m_linkCount);
  file->Read(m_zfgDelay);
  uint32_t n = 0;
  file->Read(n);
  m_loopback.clear();
  for (uint32_t i = 0; i < n && i < 64; i++)
  {
    uint32_t size = 0;
    file->Read(size);
    std::vector<uint8_t> frame(std::min<uint32_t>(size, 0x4000));
    file->Read(frame.data(), (uint32_t) frame.size());
    m_loopback.push_back(std::move(frame));
  }
}
