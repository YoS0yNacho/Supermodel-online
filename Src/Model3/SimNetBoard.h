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

#ifndef INCLUDED_SIMNETBOARD_H
#define INCLUDED_SIMNETBOARD_H

#include <cstdint>
#include <chrono>
#include "NetBoardLink.h"
#include "INetBoard.h"
#include "Network/Online/SessionPolicy.h"
#include "Network/Games/NetBoardState.h"

enum class State
{
	start,
	init,
	testing,
	ready,
	error
};

enum class GameType
{
	unknown,
	one,		// all games except those listed below
	two			// Le Mans 24, Virtual-On, Dirt Devils
};

class CSimNetBoard : public INetBoard
{
public:
	CSimNetBoard(const Util::Config::Node& config, std::shared_ptr<CNetServer> server);
	~CSimNetBoard(void);

	void SaveState(CBlockFile* SaveState);
	void LoadState(CBlockFile* SaveState);

	Result Init(uint8_t* netRAMPtr, uint8_t* netBufferPtr);
	void RunFrame(void);
	bool OnlineSessionFailed() override { return m_relay && (m_onlineFailed.load() || (m_link && m_link->RelayIndex() == -2)); }
	int RelayCabinetIndex() override { return m_link ? m_link->RelayIndex() : -1; }
	unsigned SyncCabinetCount() const override { return m_numMachines; }
  int SyncCabinetIndex() const override { return m_state==State::ready ? int(m_asyncIndex[0]) : -1; }
	bool Srally2PeersReady() const override;
	void SetRelayNativeReady() override { m_relayNativeReady = true; }
  void SetRaceParticipants(uint32_t mask) override { m_gameNet.m_nativeParticipants=mask; }
  uint32_t NativeEntryParticipants() const override { return m_gameNet.lemans24.entry.locked; }
	bool HoldForRaceStart(bool atGate, bool nativeEntry = false) override;
	void SetDaytona2CpuRouting(const Dayto2pe::CpuRouting& routing) override { m_gameNet.daytona.m_cpuRouting = routing; }
	void SetScudRouting(const Scud::Routing& r) override { m_gameNet.scud.routing=r; }
	// midSession: an adaptive host changed the rate while racing; keep the race
	// pacing state instead of restarting the 60 Hz settling window.
	void SetTickRate(unsigned hz, bool midSession = false);
	unsigned GetTickRate() const { return m_tickRateHz; }
	void Reset(void);

	bool IsAttached(void);
	bool IsRunning(void);

	void GetGame(const Game& gameInfo);
	// Sega Rally 2 drives the link board through a command/status port (IO
	// register 0x88): a command is written and then polled until bit 15 clears.
	bool IsCommandPortMode() const { return m_commandPort; }

	uint8_t ReadCommRAM8(unsigned addr);
	uint16_t ReadCommRAM16(unsigned addr);
	uint32_t ReadCommRAM32(unsigned addr);

	void WriteCommRAM8(unsigned addr, uint8_t data);
	void WriteCommRAM16(unsigned addr, uint16_t data);
	void WriteCommRAM32(unsigned addr, uint32_t data);

	uint16_t ReadIORegister(unsigned reg);
	void WriteIORegister(unsigned reg, uint16_t data);

private:
	// Config
	const Util::Config::Node& m_config;

	uint8_t* RAM = nullptr;
	uint8_t* Buffer = nullptr;
	uint8_t* CommRAM = nullptr;
	uint8_t* externalCommRAM = nullptr;

	// netsock
	uint16_t port_in = 0;
	uint16_t port_out = 0;
	std::string addr_out;
	std::thread m_connectThread;
	std::atomic_bool m_quit = false;
	std::atomic_bool m_connected = false;

	std::shared_ptr<CNetServer> m_server;
	std::unique_ptr<CNetBoardLink> m_link;

	Game m_gameInfo;
	GameType m_gameType = GameType::unknown;
	State m_state = State::start;

	uint8_t m_numMachines = 0;
	uint16_t m_counter = 0;

	uint16_t m_segmentSize = 0;

	bool m_attached = false;

	// srally2 command/status port emulation (see Src/Network/Games/Srally2/Policy.h)
	bool m_commandPort = false;
	unsigned m_commandPoll = 0;
	bool m_running = false;

	uint16_t m_IRQ2ack = 0;
	uint16_t m_status0 = 0;	// ioreg 0x88
	uint16_t m_status1 = 0;	// ioreg 0x8a
	unsigned m_poll = 0;
	bool m_commbank = false;

	// Async state is owned exclusively by the emulation thread. Socket I/O is
	// already performed by CNetServer/CTCPConnection, never against guest RAM.
	std::atomic<bool> m_onlineFailed{false};
	bool m_relay = false, m_video60 = false, m_relayNativeReady = false;
	unsigned FrameMilliHz() const { return Online::FrameMilliHz(m_relay, m_video60); }
	double FrameDurationUs() const { return Online::FrameDurationUs(m_relay, m_video60); }
	bool m_async = false;
	enum class AsyncPhase { start, guid, verify, index, count };
	AsyncPhase m_asyncPhase = AsyncPhase::start;
	bool m_asyncMaster = false;
	bool m_asyncRelay = false;
	uint8_t m_asyncIndex[2] = {};
	uint8_t m_asyncCount[2] = {};
	uint64_t m_asyncGUID = 0;
	unsigned m_asyncReceived = 0;
	bool m_asyncInFlight = false;
	unsigned m_asyncInFlightCount = 0, m_asyncUnackedFromPeer = 0;
	unsigned m_tickRateHz = 60, m_tickPhase = 60000;
	unsigned m_exchangeRateHz = 60; // Reliable origin pacing; negotiated/UDP rate stays separate.
	uint64_t m_asyncLastSendUs = 0, m_asyncLastSentSourceUs = 0;
	std::deque<uint64_t> m_asyncSendFrames;
	std::vector<uint8_t> m_asyncLastSent;
	std::vector<uint8_t> m_asyncWorking;
	std::vector<uint8_t> m_asyncLatest;
	// Per-source timestamp measurements published atomically with a complete ring.
	std::vector<uint64_t> m_asyncWorkingAgeUs, m_asyncWorkingUncertaintyUs, m_asyncWorkingReceiveUs;
	std::vector<uint64_t> m_asyncLatestAgeUs, m_asyncLatestUncertaintyUs, m_asyncLatestSourceUs;
	std::vector<uint64_t> m_asyncKeyframeSourceUs;  // stamp of the last TCP segment per slot
	uint64_t m_asyncLastTcpSourceUs = 0;            // stamp of our last originated TCP segment
	uint64_t m_asyncLastClockPingUs = 0;
	std::chrono::steady_clock::time_point m_asyncProgress;

	GameNetwork::NetBoardState m_gameNet;
	void InitializeGameNet();
	bool ConfigureGameNet();
	uint64_t m_udpNextSendUs = 0;
	bool m_udpLogged = false;
	// UDP-primary racing: when every cabinet confirms the UDP lane, in-race
	// snapshots travel only by UDP and the TCP ring carries just state-change
	// keyframes and race-start control. Emulation thread only.
	std::vector<uint8_t> m_asyncKeyframe;      // last TCP segment per slot (race-context reference)
	std::vector<uint8_t> m_asyncKeyframeValid; // one flag per slot
	uint64_t m_udpReadySinceUs = 0;
	bool m_udpPrimary = false;                 // TCP ring currently silent
	uint64_t m_statTcpOriginated = 0, m_statUdpApplied = 0, m_statTcpStaleSkipped = 0;
	bool UdpSnapshotCapable() const;
	bool UdpPrimarySilent(uint64_t nowUs);
	// Message classification for the silent ring: bytes that change on most
	// ticks are streamed state (positions, timers) and travel by UDP only; a
	// change in any other byte is a control message and is sent by TCP.
	std::vector<uint8_t> m_udpByteActivity, m_udpClassSample, m_udpStreamed;
	uint64_t m_udpClassNextUs = 0, m_udpControlLogUs = 0;
	uint64_t m_linkStatsLogUs = 0; // last periodic link summary (InfoLog)
	uint64_t m_statTcpControl = 0, m_statTcpControlLogged = 0, m_statUdpHeldForControl = 0;
	void ClassifyOutgoing(uint64_t nowUs);
	bool ControlMessagePending() const;
	std::vector<uint32_t> m_udpControlHist;
	void NoteControlMessage();
	void ApplyUdpSnapshots(uint64_t nowUs);
	// Streamed-word mask of the outgoing packet (RelayUdp::MaskSize bytes), empty until classified.
	std::vector<uint8_t> StreamedMask() const;
	// Copy the words NOT flagged in mask from src into dst (both m_segmentSize bytes).
	void MergeControlWords(unsigned slot, const uint8_t* src);
	uint64_t m_statUdpMerged = 0, m_statTcpControlMerged = 0;
	uint64_t m_statRingRttSumUs = 0, m_statRingRttCount = 0; // own segment: send stamp -> self-echo
	uint64_t m_statUdpTransitSumUs = 0, m_statUdpTransitCount = 0; // UDP snapshot: source stamp -> arrival (relay clock)
	uint64_t m_statUdpPrimaryDrops = 0; // times the lane fell back from UDP-only to the ring
	uint64_t m_statUdpPresentation = 0, m_statUdpPresentationTimed = 0; // predictor inputs taken from a UDP snapshot (and of those, with a relay-clock age)
	// When the game last wrote the local segment of each CommRAM bank (bank 0 =
	// Buffer, 1 = Buffer + 0x10000), so a snapshot can carry the age of its
	// data instead of the time it was picked up. Written on the emulation
	// thread through WriteCommRAM*; cleared when the bank becomes external.
	uint64_t m_bankWriteFirstUs[2] = {}, m_bankWriteLastUs[2] = {};
	void NoteSegmentWrite(unsigned addr);
	void SampleTiming(uint64_t nowUs, uint64_t& ageUs, uint64_t& uncertaintyUs) const;
	// Relay cabinet index whose segment occupies CommRAM receive slot `slot`.
	unsigned UdpOriginForSlot(unsigned slot) const { return (unsigned(m_asyncIndex[0]) + m_numMachines - 1 - slot) % m_numMachines; }
	uint64_t NextSourceStampUs(uint64_t nowUs);
	const uint8_t* PresentationSource(size_t,std::vector<uint8_t>&,double&,double&,uint64_t* = nullptr);
	void ApplyGamePrediction();
	void ApplyDaytonaPrediction();
	void ConfigureScudPrediction();
	void ApplyScudPrediction();
	void ConfigureSrally2Prediction();
	void ApplySrally2Prediction();
	void ConfigureLemans24Prediction();
	void ApplyLemans24Prediction();
	void RecordGameSnapshot();
	void LogGameTransportReady();
	void TickStartSync();
	void ReceiveStartToken(const std::vector<uint8_t>& data);
	void SendStartToken(uint32_t kind, uint64_t maxRTT = 0);
	void ResetStartSync();

	void RunAsyncTesting();
	void FinishAsyncTesting();
	void RunAsyncExchange();
	unsigned NativeExchangeTickRate(uint64_t nowUs);
	void AsyncError(const char* reason);
	bool AsyncTimedOut();

	inline bool IsGame(const char* gameName);
	void ConnectProc(void);
};

#endif