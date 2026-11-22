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
 * Model2Comm.h
 *
 * The Model 2 communication board (837-10537, and the EPR-18643 board of the
 * later games), ported from MAME's m2comm with M2COMM_SIMULATION (Ariane
 * Fugmann; src/mame/sega/m2comm.cpp): the board's Z80 is not run, its link
 * protocol is simulated. The cabinets make a ring; each sends frames to the
 * next one:
 *
 *    FF id count   link setup from the master; each slave takes the next id
 *    FE id count   link established
 *    FC            vsync, from the master once per frame
 *    id data...    one cabinet's part of the ring buffer (shared memory
 *                  2000-), which each cabinet copies to frame offset (1C0)
 *
 * The frames go to the next machine through CNetBoardLink, Supermodel's ring
 * connection (the Network options, as the Model 3 net board). Without the
 * network the board is linked to itself, which is what MAME does with no
 * other machine on its sockets: a master runs the link alone.
 *
 * Based on original work by Ariane Fugmann in MAME. Portions Copyright (c)
 * Ariane Fugmann, used under the BSD 3-Clause License: see Docs/README.txt.
 */

#ifndef INCLUDED_MODEL2COMM_H
#define INCLUDED_MODEL2COMM_H

#include "Network/NetServer.h"
#include "Network/Games/Sgt24h/Freshness.h"
#include "Network/Games/Sgt24h/Pose.h"
#include "Network/Games/Sgt24h/CopyContext.h"
#include <array>
#include <mutex>
#include "Network/Online/IndyRaceEntry.h"
#include "Network/Games/Indy500/RaceUdp.h"
#include "Network/Games/Srallyc/Motion.h"
#include "Network/Games/Daytona/Motion.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <chrono>
#include <deque>
#include <memory>
#include <string>
#include <thread>
#include <vector>

class CBlockFile;
class CNetBoardLink;

class CModel2Comm
{
public:
  // shared memory (01A00000-01A03FFF), cn (01A04000), fg (01A04002)
  uint8_t ReadShared(unsigned offset) const
  {
    offset &= 0x3fff;
    const unsigned rx=offset-IndyMotion::Rx;
    return m_indyGuestViewActive && rx<m_indyGuestView.size() ? m_indyGuestView[rx] : m_shared[offset];
  }
  void WriteShared(unsigned offset,uint8_t data)
  {
    offset &= 0x3fff;
    m_shared[offset]=data;
    const unsigned rx=offset-IndyMotion::Rx;
    if (m_indyRaceUdp && rx<m_indyGuestView.size())
    {
      // Preserve guest writes and wait for the next ordered row before
      // substituting any position for this row again.
      const unsigned row=rx/IndyMotion::RowBytes;
      m_indyGuestWriteRows|=uint8_t(1u<<row);
      m_indyAppliedMotion[row]=m_indyWireMotion[row]={};
      if (m_indyGuestViewActive)
      {
        const unsigned base=row*IndyMotion::RowBytes;
        std::copy_n(m_shared+IndyMotion::Rx+base,IndyMotion::RowBytes,m_indyGuestView.data()+base);
      }
    }
  }
  uint8_t ReadCN() const { return m_cn | 0xfe; }
  void    WriteCN(uint8_t data);
  uint8_t ReadFG();
  void    WriteFG(uint8_t data) { m_fg = data & 1; }

  // At vblank (MAME's check_vint_irq)
  void Tick(bool indyRaceContext=false,unsigned indyMotionSlots=0);

  void Reset();
  void SaveState(CBlockFile *file);
  void LoadState(CBlockFile *file);

  /*
   * Init(server, address, port, frameSync):
   *
   * With a server, frames go to the next machine at address:port; without
   * one the board is linked to itself. frameSync: MAME's -comm_framesync, the
   * machines wait for each other's vsync (lock step). A Model 2 lobby ticket
   * routes to the central relay instead; native vsync waiting stays disabled.
   */
  void Init(std::shared_ptr<CNetServer> server, const std::string &address, uint16_t port, bool frameSync,
            const std::string &lobbyTicket = "", uint64_t relayGuid = 0, bool model2PoseUdp = false,
            unsigned expectedSeat = 0, unsigned expectedCount = 0, bool video60 = false);

  double FramePeriodUs() const { return m_framePeriodUs; }
  bool Networked() const { return m_link != nullptr; }
  bool OnlineSessionFailed() const;
  bool SrallycOnline() const { return m_srallycOnline; }
  bool DaytonaOnline() const { return m_daytonaOnline; }
  bool Sgt24hOnline() const { return m_sgt24hOnline; }
  // Read-only application token. Never consulted by native ring delivery.
  // Token lifetime is confined to this comm instance and reset epoch.
  Sgt24h::FrameToken Sgt24hAppliedToken() const;
  void LatestSgt24hPose(unsigned cabinet,Sgt24hPose::Received& out);
  bool Sgt24hAppliedCurrent(Sgt24h::FrameToken token,uint64_t nowUs,uint64_t maxAgeUs) const;
  void ObserveSgt24hRxCopy(uint32_t pc,uint32_t rip,uint32_t source,
                          uint32_t destination,uint32_t address,uint32_t mask);
  // Zero-based cabinet. A witness is a final source read, not copy completion.
  Sgt24h::FrameToken Sgt24hCopyToken(unsigned cabinet,uint64_t nowUs,uint64_t maxAgeUs) const;
  unsigned NativeRelayCount() const;
  unsigned DaytonaMotionSeat() const;
  unsigned DaytonaMotionCount() const { return m_linkCount; }
  DaytonaMotion::Row DaytonaRow(unsigned offset,unsigned seat,DaytonaMotion::Scene scene) const;
  void DaytonaRaceScene(DaytonaMotion::Scene scene);
  void LatestDaytonaPose(unsigned seat,DaytonaMotion::Received&);
  unsigned NativeRelaySeat(); // validates both Ready messages against the GUI roster
  void NativeRoleConfigured();
  unsigned SrallycMotionSeat() const;
  SrallycMotion::Row SrallycMotionRow(unsigned offset) const;
  // Sega Rally: this cabinet's own car appears in the RX ring as an echo row
  // that only becomes current after a full round trip through the relay.
  // Map a read of that echo row onto the live local TX row so the drawn car
  // does not wait on the network. Returns false for every other offset.
  bool SrallycLocalMirror(unsigned offset, unsigned* source) const;
  // Offset of this cabinet's own echo row inside the RX ring.
  unsigned SrallycEchoOffset() const;

  void LatestSrallycPose(unsigned seat,SrallycMotion::Received& out);
  void FailNativeLink(const char* reason);
  bool IndyOnline() const { return m_indyOnline; }
  unsigned IndyRelaySeat() const; // 1-based, or 0 until both relay channels are ready
  // Read-only hints for presentation, never consulted by native playout.
  uint64_t IndyPresentationAgeUs(unsigned row) const
  {
    return uint64_t(m_indyPresentationDwellUs) + (row+1) * m_indyPresentationHalfRttUs;
  }
  IndyPoseUdp::Race IndyUdpRace() const { return m_indyUdpRace; }
  bool IndyUdpActive() const; // legacy IPU1 presentation-only lane
  bool IndyRaceUdpEnabled() const { return m_indyRaceUdp; }
  struct IndyMotionSource { uint32_t tick=0; uint64_t sampleUs=0; };
  IndyMotionSource IndyRemoteMotion(unsigned row,unsigned slot) const
  {
    return row<m_indyAppliedMotion.size() && slot<IndyRaceUdp::Slots ? m_indyAppliedMotion[row][slot] : IndyMotionSource{};
  }
  void SendIndyPose(const IndyPoseUdp::Bytes& bytes,uint64_t now);
  bool LatestIndyPose(unsigned peer,IndyPoseUdp::Received& out);
  enum class Entry { Wait, Release, Failed };
  Entry GateIndyEntry(uint32_t roster, unsigned course, uint64_t frame);
  bool FailIndyEntry(const char* reason);

  CModel2Comm();
  ~CModel2Comm();

private:
  void ReadFGLink();
  bool ConfigureIndyOnline();
  bool EstablishLink();
  void ClearIndyPlayout();
  void TickIndyPlayout(int frameStart, int frameSize, int frameOffset, int dataSize, bool isMaster, bool isSlave, bool raceContext, unsigned slots);
  void PollIndyRaceUdp(const uint8_t* local,uint64_t now,bool raceContext,unsigned slots);
  void ClearIndyRaceUdp();
  struct IndyPair;
  bool ApplyIndyRacePair(IndyPair&,int frameOffset,uint64_t now,bool driving);
  void PublishIndyRaceMotion(int frameOffset,uint64_t now,bool driving);
  void SendIndyPair(int frameStart,int frameSize,int dataSize,const std::vector<uint8_t>* sync,uint64_t now);
  void SetIndyMotionPrimary(bool enabled,uint64_t now);
  void FailIndyPlayout(const char* reason);
  bool DrainIndyPairs(int dataSize, uint8_t other);
  uint64_t IndyOldestReceived() const;
  bool CheckIndyAge(uint64_t nowUs, uint64_t receivedUs);
  void TickIndyEntry();
  bool ReceiveIndyEntry(const std::vector<uint8_t>& frame);
  void SendIndyEntry(const IndyRaceEntry::Control& control);
  bool m_srallycOnline = false, m_daytonaOnline = false, m_nativeRoleReady = false;
  bool m_sgt24hOnline = false;
  mutable std::mutex m_sgt24hFreshnessMutex;
  Sgt24h::Freshness m_sgt24hFreshness;
  Sgt24hPose::Sender m_sgt24hPoseSender;
  std::array<Sgt24h::FrameToken,Sgt24h::Cabinets> m_sgt24hCopies{};
  uint64_t m_sgt24hEpoch=0;
  void ResetSgt24hFreshness();
  void ClearSgt24hFreshness();
  void ObserveSgt24hApplication(int frameOffset,int frameSize,int receivedSize);
  SrallycMotion::Publisher m_srallycMotionTx;
  DaytonaMotion::Publisher m_daytonaMotionTx;
  DaytonaMotion::Scene m_daytonaScene;
  unsigned m_expectedSeat = 0, m_expectedCount = 0, m_nativeSeat = 0;
  double m_framePeriodUs = IndyMotion::FrameUs; // session pacing, not a wire field
  std::chrono::steady_clock::time_point m_nativeLinkDeadline{}, m_daytonaFfRetry{};
  unsigned m_daytonaObservedRole=256;
  bool m_indyOnline = false, m_indyConfigured = false, m_indyEntryInitialized = false;
  uint64_t m_indyLastPingUs = 0;
  uint64_t m_indyPresentationHalfRttUs = 0;
  double m_indyPresentationDwellUs = 0;
  IndyRaceEntry::Barrier m_indyEntry;
  IndyPoseUdp::Race m_indyUdpRace;
  void RecordIndyAge(uint64_t ageUs);
  unsigned IndyAgePercentile(unsigned percentile) const;
  struct IndyPair
  {
    std::vector<uint8_t> data, sync;
    IndyRaceUdp::Holes holes{};
    uint64_t arrivedUs = 0;
  };
  std::deque<IndyPair> m_indyFifo;
  IndyPair m_indyPartial;
  struct IndyPeerMotion
  {
    IndyRaceUdp::Sample sample;
    uint64_t stamp=0, heardUs=0, sampleUs=0;
    static bool Fresh(uint64_t stamp,uint64_t now,uint64_t limit) { return stamp && now>=stamp && now-stamp<=limit; }
    bool FreshMotion(uint64_t now,uint64_t limit) const { return Fresh(sampleUs,now,limit); }
    bool FreshPresence(uint64_t now,uint64_t limit) const { return Fresh(heardUs,now,limit); }
  };
  std::array<IndyPeerMotion,Indy500Online::Cabinets> m_indyPeers{};
  std::array<std::array<IndyRaceUdp::Known,IndyRaceUdp::Slots>,Indy500Online::Cabinets> m_indyKnown{};
  // Keep the ordered ring image intact, even when the local guest sees a
  // more recent verified pose, or no new TCP pair arrived at this vblank.
  std::array<std::array<IndyRaceUdp::Known,IndyRaceUdp::Slots>,Indy500Online::Cabinets> m_indyVisible{};
  using IndyMotionSources=std::array<std::array<IndyMotionSource,IndyRaceUdp::Slots>,Indy500Online::Cabinets>;
  IndyMotionSources m_indyWireMotion{}, m_indyAppliedMotion{};
  std::array<uint8_t,IndyMotion::RowBytes*Indy500Online::Cabinets> m_indyGuestView{};
  uint8_t m_indyGuestWriteRows=0;
  bool m_indyGuestViewActive=false;
  // Restore this cabinet's exact echoed coordinates instead of borrowing a
  // later local pose. Bounded by two seconds of native frames.
  std::array<IndyRaceUdp::Sample,128> m_indyLocalHistory{};
  IndyRaceUdp::Sample m_indyLocalMotion, m_indyLastKeyframe;
  IndyRaceUdp::Holes m_indyForwardHoles{};
  std::vector<uint8_t> m_indyTcpTxBase, m_indyTcpRxBase;
  std::array<uint64_t,Indy500Online::Cabinets> m_indyUnusableSince{};
  uint64_t m_indyPauseUdpUntil=0;
  bool m_indyRaceUdp=false, m_indyPrimary=false;
  uint64_t m_indyReadySince=0, m_indyLastKeyframeUs=0;
  uint32_t m_indyLastUdpTick=0;
  uint8_t m_indyLastUdpMask=0;
  uint64_t m_indyLastUdpUs=0;
  bool m_indyRaceDrove=false;
  uint64_t m_indySplitSent=0, m_indyFullSent=0, m_indyNativeUdp=0, m_indyNativeHold=0, m_indyWireBytes=0;
  uint64_t m_indyMaxQueueUs = 0;
  uint64_t m_indyPeakAgeUs = 0;
  // One bin per whole millisecond, last bin includes everything >= 1000 ms.
  std::array<unsigned, 1001> m_indyAgeBins{};
  unsigned m_indyDepth = 0;
  unsigned m_indySeedPairs = 0;  // pairs generated before the first complete return
  unsigned m_indySeedLimit = 0;  // bounded startup credits, multi-seat master only
  uint64_t m_indyFfSentUs = 0;
  bool m_indyPrimed = false;
  unsigned m_indyReceived = 0, m_indyApplied = 0, m_indyMissed = 0;
  size_t m_indyHigh = 0;
  int  ReadFrame(int dataSize, bool wait = false);
  void SendData(uint8_t frameType, int frameStart, int frameSize, int dataSize);
  void SendFrame(int dataSize);
  bool LinesOpen() const;
  void ConnectProc();

  uint8_t  m_shared[0x4000];
  uint8_t  m_zfg = 0;             // the Z80 side's flip gate (FG bit 7 reads its inverse)
  uint8_t  m_cn = 0;              // board enable
  uint8_t  m_fg = 0;              // the i960 side's flip gate
  uint8_t  m_buffer0[0x4000];
  unsigned m_onlineReadBudget=64; // shared by Tick and guest FG polls
  bool     m_frameSync = false;
  uint16_t m_frameOffset = 0x1c0; // 180 in Power Sled
  uint8_t  m_linkEnable = 0;
  uint16_t m_linkTimer = 0;
  uint8_t  m_linkAlive = 0;       // 0 linking, 1 established, 2 failed
  uint8_t  m_linkID = 0;
  uint8_t  m_linkCount = 0;
  uint8_t  m_zfgDelay = 0;

  // Linked to itself: the frames sent, received at the next read
  std::deque<std::vector<uint8_t>> m_loopback;

  // Linked to other machines
  std::unique_ptr<CNetBoardLink> m_link;
  std::thread m_connectThread;
  std::atomic_bool m_connected = false;
  std::atomic_bool m_onlineFailed = false;
  std::atomic_bool m_quit = false;
};

#endif  // INCLUDED_MODEL2COMM_H
