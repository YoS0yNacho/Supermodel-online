// Shared scheduling state and explicit per-game composition. GPL-3.0-or-later.
#ifndef INCLUDED_GAME_NETBOARD_STATE_H
#define INCLUDED_GAME_NETBOARD_STATE_H
#include "Daytona2/NetBoardState.h"
#include "Lemans24/Prediction.h"
#include "Scud/Prediction.h"
#include "Srally2/Prediction.h"
#include "RaceStart.h"
#include "NativeLinkPacing.h"
#include <chrono>
namespace GameNetwork {
struct NetBoardState {
 Dayto2pe::NetBoardState daytona;
 Lemans24::State lemans24;
 Scud::State scud;
 Srally2::State srally2;
 uint64_t guid=0, m_predictionFrame=0,m_asyncRoundStartFrame=0;
 unsigned m_asyncSnapshotAge=0;
 unsigned m_appliedTickRate=60;
 NativeLinkPacing nativePacing;
	std::chrono::steady_clock::time_point m_linkInitTime;

	// Scheduled, in-game-entry barrier. All fields belong to the emulation thread.
	bool m_syncStart = false;
	bool m_timedSegments = false;
	bool m_preserveRingSelfEcho = false;
	bool m_startWaiting = false, m_startReleased = false, m_startFailed = false;
	bool m_startPrepared = false, m_startCommitted = false, m_startTokenPending = false;
	uint32_t m_nativeParticipants=0, m_startParticipants=0, m_leaderEpoch=0;
  std::array<uint32_t,16> m_lastAccepted{};
  std::array<RaceStart::Transit,16> m_startTransit{};
	uint32_t m_startEpoch = 0, m_startSerial = 0, m_startKind = 0;
	uint64_t m_startWaitTime = 0, m_startNextProbe = 0, m_startTokenTime = 0;
	uint64_t m_startLastPing = 0;
	// Release lead is derived from measured token rounds; a prepare that
	// arrives too close to its deadline is declined and retried with a longer lead.
	uint64_t m_startRoundMaxUs = 0;
	uint32_t m_startLeadRetries = 0;
	int64_t m_startDeadline = 0;
	int64_t m_startPeerOffset = 0;

 bool Enabled() const { return m_syncStart; }
 bool UseTimedSegments() const { return m_timedSegments || daytona.m_daytoPrediction; }
 bool ImmediateSelfEcho() const { return !m_preserveRingSelfEcho; }
 uint64_t ProtocolGUID(uint64_t fallback) const { return m_syncStart ? guid : fallback; }
 // Reserve a distinct async handshake identity for the timed-sidecar format.
 // Keep the relay/lobby GUID exact-ROM; only cabinet-to-cabinet setup sees this tag.
 uint64_t AsyncProtocolGUID(uint64_t fallback) const {
  const uint64_t id=ProtocolGUID(fallback);
  return UseTimedSegments() ? id ^ 0x44544d3200000000ULL : id;
 }
 unsigned ControlBudget() const { return m_syncStart ? 4u : 0u; }
 bool IsControlPacket(size_t size) const { return m_syncStart && size==sizeof(RaceStart::StartToken); }
 void OnFrame() { ++m_predictionFrame; }
 void OnExchangeStarted() { m_asyncRoundStartFrame=m_predictionFrame; }
 void OnFailure() { if(m_syncStart && m_startWaiting) m_startFailed=true; }
 void SetTickRate(unsigned hz) { m_appliedTickRate=hz; daytona.SetTickRate(hz); lemans24.SetTickRate(hz); scud.SetTickRate(hz); srally2.SetTickRate(hz); }
 void ResetPresentation() { daytona.ResetPresentation();lemans24.Reset();scud.Reset();srally2.Reset();m_predictionFrame=m_asyncRoundStartFrame=0;m_asyncSnapshotAge=0; }
};
}
#endif
