/**
 ** Supermodel
 ** A Sega Model 3 Arcade Emulator.
 ** Copyright 2011-2020 Bart Trzynadlowski, Nik Henson, Ian Curtis,
 **                     Harry Tuttle, and Spindizzi
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

// Game-specific hooks; shared transport remains in SimNetBoard.
#include "Supermodel.h"
#include "Model3/SimNetBoard.h"

#include "Policy.h"
#include "UdpPresentationPolicy.h"
#include "Srally2/MultiStartResearch.h"
#include <cstdlib>
bool CSimNetBoard::Srally2PeersReady() const {
  return Srally2::Research::RingEligible(m_asyncLatest.data(),m_asyncLatest.size(),m_numMachines,m_asyncIndex[0]);
}
void CSimNetBoard::InitializeGameNet()
{
 m_gameNet.daytona.m_daytoPrediction=Dayto2pe::AsyncEnabled(m_gameInfo.name,m_async);
 m_gameNet.m_syncStart=m_async && GameNetwork::IsSupportedGame(m_gameInfo.name);
 m_gameNet.m_timedSegments=m_gameNet.m_syncStart || m_gameNet.daytona.m_daytoPrediction;
 m_gameNet.m_preserveRingSelfEcho=m_async && Srally2::PreserveRingSelfEcho(m_gameInfo.name);
 m_gameNet.guid=GameNetwork::GUID(m_gameInfo.name,Srally2Research::Selected(m_config));
}
bool CSimNetBoard::ConfigureGameNet()
{
 m_gameNet.ResetPresentation();ResetStartSync();
 if(m_gameNet.m_syncStart && (m_segmentSize!=GameNetwork::PacketSize(m_gameInfo.name) || m_numMachines>GameNetwork::MaxCabinets(m_gameInfo.name,Srally2Research::Selected(m_config)))) {
  AsyncError("game-specific async networking requires the exact native packet size and supported cabinet count");return false;
 }
 if(m_gameNet.m_syncStart) {
  if (!Srally2::IsSupportedGame(m_gameInfo.name))
   InfoLog("%s scheduled in-game-entry synchronization enabled",m_gameInfo.name.c_str());
  InfoLog("%s HLE link ready after %.3f ms",m_gameInfo.name.c_str(),std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-m_gameNet.m_linkInitTime).count());
 }
 if(m_gameNet.daytona.m_daytoPrediction) {
  m_gameNet.daytona.m_daytoPredictors.resize(m_numMachines-1);
  m_gameNet.daytona.m_cpuPredictors.resize(m_numMachines-1);
  m_gameNet.daytona.SetTickRate(m_tickRateHz);
  InfoLog("%s experimental position prediction enabled (%u remote cabinets)",m_gameInfo.name.c_str(),unsigned(m_numMachines-1));
 }
 if(m_gameNet.m_syncStart && Scud::IsSupportedGame(m_gameInfo.name)) ConfigureScudPrediction();
 else if(m_gameNet.m_syncStart && Srally2::IsSupportedGame(m_gameInfo.name)) ConfigureSrally2Prediction();
 else if(m_gameNet.m_syncStart && Lemans24::IsSupportedGame(m_gameInfo.name)) ConfigureLemans24Prediction();
 m_gameNet.m_appliedTickRate = m_tickRateHz;
 return true;
}
void CSimNetBoard::ApplyGamePrediction() {
 if(m_gameNet.m_appliedTickRate != m_tickRateHz)
  m_gameNet.SetTickRate(m_tickRateHz);
 if(m_gameNet.daytona.m_daytoPrediction) ApplyDaytonaPrediction();
 else if(m_gameNet.m_syncStart && Scud::IsSupportedGame(m_gameInfo.name)) ApplyScudPrediction();
 else if(m_gameNet.m_syncStart && Srally2::IsSupportedGame(m_gameInfo.name)) ApplySrally2Prediction();
 else if(m_gameNet.m_syncStart && Lemans24::IsSupportedGame(m_gameInfo.name)) ApplyLemans24Prediction();
}
void CSimNetBoard::RecordGameSnapshot() {
 m_gameNet.m_asyncSnapshotAge=unsigned(std::min<uint64_t>(m_gameNet.m_predictionFrame-m_gameNet.m_asyncRoundStartFrame,18));
}
void CSimNetBoard::LogGameTransportReady() {
 if(m_gameNet.m_syncStart) InfoLog("%s transport ready after %.3f ms",m_gameInfo.name.c_str(),std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-m_gameNet.m_linkInitTime).count());
}

unsigned CSimNetBoard::NativeExchangeTickRate(uint64_t nowUs)
{
  if(!m_relay || !GameNetwork::NeedsNativeSetupPacing(m_gameInfo.name) || m_tickRateHz>=60)
    return m_tickRateHz;
  bool racing=!m_gameNet.m_startWaiting && m_numMachines>=2 &&
    m_asyncLatest.size()==size_t(m_numMachines)*m_segmentSize &&
    m_asyncWorkingReceiveUs.size()==m_numMachines;
  GameNetwork::NativeRaceContext local{};
  racing=racing && GameNetwork::ReadNativeRaceContext(m_gameInfo.name,CommRAM+0x100,
    m_segmentSize,m_asyncIndex[0],m_numMachines,local);
  uint64_t key=local.shared ^ (local.identity*0x9e3779b97f4a7c15ULL);
  for(unsigned slot=0;racing && slot+1<m_numMachines;++slot) {
    const unsigned peer=(unsigned(m_asyncIndex[0])+m_numMachines-1-slot)%m_numMachines;
    GameNetwork::NativeRaceContext remote{};
    const uint64_t received=m_asyncWorkingReceiveUs[slot];
    racing=received && nowUs>=received && nowUs-received<=GameNetwork::NativeLinkPacing::MaxPeerSilenceUs &&
      GameNetwork::ReadNativeRaceContext(m_gameInfo.name,m_asyncLatest.data()+size_t(slot)*m_segmentSize,
        m_segmentSize,peer,m_numMachines,remote) && remote.shared==local.shared;
    key=(key ^ remote.identity)*0x100000001b3ULL;
  }
  return m_gameNet.nativePacing.Select(m_tickRateHz,racing,key);
}

namespace
{
uint64_t StartSyncNow()
{
	return uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count());
}
bool FreshStartClock(const CTCPConnection::ClockSample& clock, uint64_t now)
{
	return clock.rtt >= 0 && clock.rtt <= 2000000 && clock.received <= now &&
		clock.received != 0 && now - clock.received <= 3000000;
}
}

void CSimNetBoard::ResetStartSync()
{
  m_gameNet.m_nativeParticipants=m_gameNet.m_startParticipants=m_gameNet.m_leaderEpoch=0;
  m_gameNet.m_lastAccepted.fill(0); m_gameNet.m_startTransit.fill({});
	m_gameNet.nativePacing.Reset();
	m_exchangeRateHz=60;
	m_gameNet.m_startWaiting = m_gameNet.m_startReleased = m_gameNet.m_startFailed = false;
	m_gameNet.m_startPrepared = m_gameNet.m_startCommitted = m_gameNet.m_startTokenPending = false;
	m_gameNet.m_startEpoch = m_gameNet.m_startSerial = m_gameNet.m_startKind = 0;
	m_gameNet.m_startWaitTime = m_gameNet.m_startNextProbe = m_gameNet.m_startTokenTime = m_gameNet.m_startLastPing = 0;
	m_gameNet.m_startDeadline = m_gameNet.m_startPeerOffset = 0;
}

bool CSimNetBoard::HoldForRaceStart(bool atGate, bool nativeEntry)
{
	if (!m_gameNet.m_syncStart) return false;
	if (m_gameNet.m_startFailed || (atGate && m_state == State::error)) return true;
	if (m_state != State::ready || m_numMachines < 2) return false;
	if (!atGate)
	{
		if (m_gameNet.m_startWaiting && !m_gameNet.m_startReleased)
		{
			AsyncError("race-start checkpoint changed while held; restart all cabinets");
			return true;
		}
		if (m_gameNet.m_startReleased)
		{
			if (nativeEntry) InfoLog("%s start ENTRY_ADVANCED cabinet=%u epoch=%u clock_us=%llu", m_gameInfo.name.c_str(),
				unsigned(m_asyncIndex[0]), m_gameNet.m_startEpoch, (unsigned long long)StartSyncNow());
			m_gameNet.m_startWaiting = m_gameNet.m_startReleased = m_gameNet.m_startPrepared = m_gameNet.m_startCommitted = false;
		}
		return false;
	}
	if (m_gameNet.m_startReleased) return false; // same checkpoint may span more than one PPC frame
	const uint64_t now = StartSyncNow();
	if (!m_gameNet.m_startWaiting)
	{
		m_gameNet.m_startWaiting = true;
    m_gameNet.m_startParticipants=m_gameNet.m_nativeParticipants;
    if (!RaceStart::ValidRoster(m_gameNet.m_startParticipants,m_numMachines,m_asyncIndex[0])) {
      AsyncError("cannot validate native race participants at start checkpoint; no guessed roster"); return true;
    }
    m_gameNet.m_startEpoch=0;
    if (RaceStart::Leader(m_gameNet.m_startParticipants)==m_asyncIndex[0])
      m_gameNet.m_startEpoch=++m_gameNet.m_leaderEpoch;
		m_gameNet.m_startSerial = 0;
		m_gameNet.m_startWaitTime = now;
		m_gameNet.m_startNextProbe = now;
		m_gameNet.m_startPrepared = m_gameNet.m_startCommitted = m_gameNet.m_startTokenPending = false;
		m_gameNet.m_startDeadline = 0;
		InfoLog("%s start READY cabinet=%u epoch=%u clock_us=%llu", m_gameInfo.name.c_str(),
			unsigned(m_asyncIndex[0]), m_gameNet.m_startEpoch, (unsigned long long)now);
	}
  // Experimental start assist is used only when explicitly enabled.
  if (m_gameInfo.name == "srally2" && std::getenv("SRALLY_EXPERIMENTAL_GATE") &&
      !m_asyncMaster && m_numMachines == 2 && m_asyncLatest.size() >= 0x70) {
    memcpy(CommRAM + 0x100 + 0x14, m_asyncLatest.data() + 0x14, 4);
    memcpy(CommRAM + 0x100 + 0x6c, m_asyncLatest.data() + 0x44, 4);
  }
  if (m_gameInfo.name == "srally2" && std::getenv("SRALLY_EXPERIMENTAL_GATE") &&
      !m_asyncMaster && m_numMachines > 2 && m_numMachines <= 4) {
    Srally2::Research::MirrorParticipantMulti(CommRAM+0x100, m_segmentSize,
      m_asyncLatest.data(),m_asyncLatest.size(),m_numMachines,m_asyncIndex[0]);
  }
	// Poll bounded transport while the guest is held.
	RunAsyncExchange();
	if (m_gameNet.m_startFailed || m_state == State::error) return true;
	const uint64_t after = StartSyncNow();
	if (m_gameNet.m_startCommitted && after >= uint64_t(m_gameNet.m_startDeadline))
	{
		m_gameNet.m_startReleased = true;
		m_tickPhase = FrameMilliHz();
		for (auto& predictor : m_gameNet.daytona.m_daytoPredictors) predictor.Reset();
		for (auto& predictor : m_gameNet.daytona.m_cpuPredictors) predictor.Reset();
		for (auto& predictor : m_gameNet.lemans24.predictors) predictor.Reset();
		for (auto& predictor : m_gameNet.scud.predictors) predictor.Reset();
		for (auto& predictor : m_gameNet.srally2.predictors) predictor.Reset();
		InfoLog("%s start RELEASE cabinet=%u epoch=%u clock_us=%llu deadline_us=%llu late_ms=%.3f held_ms=%.3f", m_gameInfo.name.c_str(),
			unsigned(m_asyncIndex[0]), m_gameNet.m_startEpoch, (unsigned long long)after,
			(unsigned long long)m_gameNet.m_startDeadline, double(after - m_gameNet.m_startDeadline) / 1000.0,
			double(after - m_gameNet.m_startWaitTime) / 1000.0);
		return false;
	}
	return true;
}

void CSimNetBoard::TickStartSync()
{
	if (!m_gameNet.m_syncStart || m_numMachines < 2) return;
	const uint64_t now = StartSyncNow();
	const int64_t curRttUs = m_link->GetIncomingClock().rtt;
	const uint64_t pingIntervalUs = m_gameNet.m_startWaiting
		? std::max<uint64_t>(300000ULL, uint64_t(std::max<int64_t>(0, curRttUs)))
		: 1000000ULL;
	if (now - m_gameNet.m_startLastPing >= pingIntervalUs)
	{
		m_link->RefreshIncomingClock();
		m_gameNet.m_startLastPing = now;
	}
	if (!m_gameNet.m_startWaiting || m_gameNet.m_startReleased) return;
	if (now - m_gameNet.m_startWaitTime > (m_gameInfo.name == "srally2" ? 120000000ULL : 60000000ULL) ||
		(m_gameNet.m_startTokenPending && now - m_gameNet.m_startTokenTime > 10000000) ||
		(m_gameNet.m_startPrepared && !m_gameNet.m_startCommitted && int64_t(now) >= m_gameNet.m_startDeadline))
	{
		AsyncError("race-start barrier timed out; no unsynchronized fallback; restart all cabinets");
		return;
	}
	if (RaceStart::Leader(m_gameNet.m_startParticipants)==m_asyncIndex[0] && !m_gameNet.m_startPrepared && !m_gameNet.m_startTokenPending && now >= m_gameNet.m_startNextProbe)
	{
		++m_gameNet.m_startSerial;
		SendStartToken(1);
	}
}

void CSimNetBoard::SendStartToken(uint32_t kind, uint64_t maxRTT)
{
	RaceStart::StartToken token;
	token.kind = kind;
	token.epoch = m_gameNet.m_startEpoch;
	token.serial = m_gameNet.m_startSerial;
  token.origin=m_asyncIndex[0]; token.participants=m_gameNet.m_startParticipants;
  token.ready=1u<<token.origin; // elected native participant, not necessarily cabinet zero
	const auto clock = m_link->GetIncomingClock();
	const uint64_t now = StartSyncNow();
	token.clocks = FreshStartClock(clock, now) ? 1u<<token.origin : 0;
	token.maxRTT = std::max(maxRTT, uint64_t(std::max<int64_t>(0, clock.rtt)));
	token.deadline = kind == 1 ? 0 : m_gameNet.m_startDeadline;
	m_gameNet.m_startKind = kind;
	m_gameNet.m_startTokenPending = true;
	m_gameNet.m_startTokenTime = now;
	m_link->Send(&token, sizeof(token));
}

void CSimNetBoard::ReceiveStartToken(const std::vector<uint8_t>& data)
{
  RaceStart::StartToken token;
  if(data.size()!=sizeof(token)) { AsyncError("invalid race-start control size"); return; }
  memcpy(&token,data.data(),sizeof(token));
  const unsigned index=m_asyncIndex[0];
  const uint32_t all=(1u<<m_numMachines)-1, bit=1u<<index;
  if(token.magic!=0x31533244 || token.kind<1 || token.kind>3 || token.reserved ||
     !token.epoch || !token.serial || !RaceStart::ValidRoster(token.participants,m_numMachines,token.origin) ||
     RaceStart::Leader(token.participants)!=token.origin ||
     token.hops!=(index+m_numMachines-token.origin-1)%m_numMachines ||
     (token.ready & ~token.participants) || (token.clocks & ~all) || token.maxRTT>2000000) {
    AsyncError("invalid native-cohort start token"); return;
  }
  const bool origin=index==token.origin, participant=(token.participants&bit)!=0;
  auto& transit=m_gameNet.m_startTransit[token.origin];
  const auto clock=m_link->GetIncomingClock(); const uint64_t now=StartSyncNow();
  const bool fresh=FreshStartClock(clock,now);
  if(fresh) token.clocks|=bit; else token.clocks&=~bit;
  const bool ready=participant && m_gameNet.m_startWaiting && !m_gameNet.m_startReleased &&
    token.participants==m_gameNet.m_startParticipants &&
    token.epoch>m_gameNet.m_lastAccepted[token.origin];
  if(ready) token.ready|=bit; else token.ready&=~bit;
  token.maxRTT=std::max(token.maxRTT,uint64_t(std::max<int64_t>(0,clock.rtt)));

  if(origin && (!m_gameNet.m_startTokenPending || token.epoch!=m_gameNet.m_startEpoch ||
     token.serial!=m_gameNet.m_startSerial || token.kind!=m_gameNet.m_startKind || !ready)) {
    AsyncError("unexpected native-cohort start acknowledgement"); return;
  }
  if(!origin && token.kind==1 && transit.prepared && !transit.committed &&
      token.epoch==transit.epoch && token.serial>transit.serial) {
    transit.prepared=false;
    if(ready) m_gameNet.m_startPrepared=false;
    m_link->RefreshIncomingClock();
  }
  if(token.kind!=1) {
    const bool same=transit.epoch==token.epoch && transit.serial==token.serial &&
                    transit.participants==token.participants;
    const int64_t offset=token.kind==3 ? (origin ? m_gameNet.m_startPeerOffset : transit.offset) : clock.offset;
    int64_t deadline=0;
    if(!fresh || (participant && !ready) ||
       (!origin && token.kind==3 && (!same || !transit.prepared || transit.committed)) ||
       !RaceStart::TranslateStartDeadline(token.deadline,offset,now,deadline)) {
      AsyncError("native-cohort deadline/clock/readiness invalid; no unsynchronized fallback"); return;
    }
    token.deadline=deadline;
    if(origin && token.kind==2) m_gameNet.m_startPeerOffset=clock.offset;
  }
  if(origin) {
    m_gameNet.m_startTokenPending=false;
    if(token.kind==1) {
      if(token.ready!=token.participants || token.clocks!=all) {
        m_gameNet.m_startNextProbe=now+250000; return;
      }
      const uint64_t lead=std::max<uint64_t>(2000000,8*(now-m_gameNet.m_startTokenTime)+500000);
      if(lead>20000000) { AsyncError("native-cohort network too slow for bounded release"); return; }
      m_gameNet.m_startDeadline=int64_t(now+lead); m_gameNet.m_startPrepared=true;
      SendStartToken(2,token.maxRTT);
    } else {
      if(token.ready!=token.participants || token.clocks!=all ||
         std::abs(token.deadline-m_gameNet.m_startDeadline)>50000) {
        if(token.kind==2) {
          m_gameNet.m_startPrepared=false; m_gameNet.m_startNextProbe=now+200000;
          m_link->RefreshIncomingClock(); return;
        }
        AsyncError("native-cohort acknowledgement or clock closure failed"); return;
      }
      if(token.kind==2) SendStartToken(3,token.maxRTT);
      else { m_gameNet.m_startCommitted=true; m_gameNet.m_lastAccepted[token.origin]=token.epoch; }
    }
    return;
  }
  if(token.kind==2) {
    if(transit.epoch>token.epoch ||
       (transit.epoch==token.epoch && transit.serial>=token.serial)) {
      AsyncError("duplicate or stale native-cohort prepare"); return;
    }
    transit={token.epoch,token.serial,token.participants,clock.offset,token.deadline,true,false};
    if(participant) {
      m_gameNet.m_startEpoch=token.epoch; m_gameNet.m_startSerial=token.serial;
      m_gameNet.m_startDeadline=token.deadline; m_gameNet.m_startPrepared=true;
      m_gameNet.m_startPeerOffset=clock.offset;
    }
  } else if(token.kind==3) {
    if(std::abs(token.deadline-transit.deadline)>50000) {
      AsyncError("inconsistent native-cohort commit"); return;
    }
    transit.committed=true;
    if(participant) {
      m_gameNet.m_startCommitted=true; m_gameNet.m_lastAccepted[token.origin]=token.epoch;
    }
  }
  // Nonparticipants forward traffic without holding their guest frames.
  ++token.hops; m_link->Send(&token,sizeof(token));
}


const uint8_t* CSimNetBoard::PresentationSource(size_t slot,std::vector<uint8_t>& storage,
 double& ageFrames,double& uncertaintyFrames,uint64_t* sourceUs)
{
 const size_t offset=slot*m_segmentSize;
 if(offset>m_asyncLatest.size() || m_segmentSize>m_asyncLatest.size()-offset) return nullptr;
 const uint8_t* reliable=m_asyncLatest.data()+offset;
 if(!m_relay || !m_link || slot+1>=m_numMachines || offset+m_segmentSize>m_asyncLatest.size()) return reliable;
 const unsigned remote=(unsigned(m_asyncIndex[0])+m_numMachines-1-unsigned(slot))%m_numMachines;
 uint64_t stamp=0,received=0;
 if(!m_link->LatestPresentation(remote,storage,stamp,received) || storage.size()!=m_segmentSize ||
    (slot<m_asyncLatestSourceUs.size() && stamp<=m_asyncLatestSourceUs[slot]) ||
    !GameNetwork::PresentationCompatible(m_gameInfo.name,reliable,storage.data(),m_segmentSize,remote,m_asyncIndex[0],m_numMachines)) return reliable;
 const uint64_t now=uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
 if(now<received) return reliable;
 // Direct UDP broadcast has no synchronized per-origin clock; transit age is unknown.
 ageFrames=double(now-received)/FrameDurationUs();
 uncertaintyFrames=60000000.0/FrameDurationUs();
 if(sourceUs) *sourceUs=stamp;
 if(!m_udpLogged) { m_udpLogged=true;InfoLog("Relay UDP r15 candidate: fresh UDP predictor inputs active; reliable control and raw ring unchanged"); }
 return storage.data();
}

// UDP-primary racing ---------------------------------------------------------
// Sega Rally 2 depends on the ring-consistent returning self echo, so it keeps
// the TCP ring at full rate and uses UDP only as predictor input (as before).
bool CSimNetBoard::UdpSnapshotCapable() const
{
 return m_relay && m_link && m_gameNet.m_syncStart && m_gameNet.UseTimedSegments() &&
   !m_gameNet.m_preserveRingSelfEcho && m_numMachines>=2;
}

// True when this cabinet may stop originating TCP ring segments this frame.
// Requires (1) every peer confirming the UDP lane both ways for one second,
// (2) no race-start barrier in progress, and (3) the local packet still being
// in the race context of the last segment sent by TCP. A context change fails
// (3), so that packet goes out reliably as a keyframe before silence resumes.
bool CSimNetBoard::UdpPrimarySilent(uint64_t nowUs)
{
 bool silent=false;
 const char* reason="UDP not confirmed by every cabinet";
 if(UdpSnapshotCapable() && m_link->UdpPrimaryReady()) {
  if(!m_udpReadySinceUs) m_udpReadySinceUs=nowUs;
  const unsigned self=m_asyncIndex[0], other=(self+1)%m_numMachines;
  if(nowUs-m_udpReadySinceUs<1000000) reason="UDP confirmation settling";
  else if(m_gameNet.m_startWaiting || m_gameNet.m_startPrepared || m_gameNet.m_startTokenPending) reason="race-start barrier";
  else if(m_asyncLastSent.size()!=m_segmentSize ||
    !GameNetwork::PresentationCompatible(m_gameInfo.name,m_asyncLastSent.data(),CommRAM+0x100,m_segmentSize,self,other,m_numMachines))
   reason="race state changed or not racing";
  else silent=true;
 } else m_udpReadySinceUs=0;
 if(silent!=m_udpPrimary) {
  m_udpPrimary=silent;
  if(silent) InfoLog("Relay UDP: race snapshots now UDP only; TCP ring silent (keyframes on state change)");
  else InfoLog("Relay UDP: TCP ring resumed (%s)",reason);
 }
 return silent;
}

// Fill each remote slot from the newest UDP snapshot. A snapshot is accepted
// only if it is newer than the slot, carries a measured age, and matches the
// race context of that cabinet's last reliable TCP segment.
void CSimNetBoard::ApplyUdpSnapshots(uint64_t nowUs)
{
 if(!UdpSnapshotCapable() || m_asyncKeyframeValid.size()!=m_numMachines ||
    m_asyncLatest.size()!=size_t(m_numMachines)*m_segmentSize) return;
 for(unsigned slot=0;slot+1<m_numMachines;++slot) {
  if(!m_asyncKeyframeValid[slot]) continue;
  const unsigned remote=(unsigned(m_asyncIndex[0])+m_numMachines-1-slot)%m_numMachines;
  CNetBoardLink::UdpSnapshot snap;
  if(!m_link->LatestSnapshot(remote,m_asyncLatestSourceUs[slot],snap) || !snap.timed ||
     snap.raw.size()!=m_segmentSize || nowUs<snap.receivedUs) continue;
  const size_t offset=size_t(slot)*m_segmentSize;
  if(!GameNetwork::PresentationCompatible(m_gameInfo.name,m_asyncKeyframe.data()+offset,snap.raw.data(),
     m_segmentSize,remote,m_asyncIndex[0],m_numMachines)) continue;
  memcpy(m_asyncLatest.data()+offset,snap.raw.data(),m_segmentSize);
  m_asyncLatestSourceUs[slot]=snap.sourceUs;
  m_asyncLatestAgeUs[slot]=snap.ageUs+(nowUs-snap.receivedUs);
  m_asyncLatestUncertaintyUs[slot]=snap.uncertaintyUs;
  m_asyncWorkingReceiveUs[slot]=nowUs;
  m_asyncProgress=std::chrono::steady_clock::now(); // the link is alive without TCP traffic
  ++m_statUdpApplied;
 }
}
