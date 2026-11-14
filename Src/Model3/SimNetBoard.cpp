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

#include <thread>
#include "Supermodel.h"
#include "SimNetBoard.h"
#include "Network/RelayProtocol.h"
#include "Network/Games/Daytona2/TimedSegment.h"
#include <OSD/Thread.h>

 // these make 16-bit read/writes much neater
#define RAM16 *(uint16_t*)&RAM
#define CommRAM16 *(uint16_t*)&CommRAM

static const uint64_t netGUID = 0x5bf177da34872;
// Never silently mix frame-locked and asynchronous cabinets.
static const uint64_t asyncNetGUID = 0x5bf177da34873;

namespace
{
uint64_t AsyncNowUs()
{
	return uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count());
}
}

inline bool CSimNetBoard::IsGame(const char* gameName)
{
	return (m_gameInfo.name == gameName) || (m_gameInfo.parent == gameName);
}

CSimNetBoard::CSimNetBoard(const Util::Config::Node& config, std::shared_ptr<CNetServer> server)
	: m_config(config),
	  m_server(std::move(server))
{
}

CSimNetBoard::~CSimNetBoard(void)
{
	m_quit = true;

	if (m_connectThread.joinable())
		m_connectThread.join();
}

void CSimNetBoard::SaveState(CBlockFile* SaveState)
{
	
}

void CSimNetBoard::LoadState(CBlockFile* SaveState)
{

}

Result CSimNetBoard::Init(uint8_t* netRAMPtr, uint8_t* netBufferPtr)
{
	RAM = netRAMPtr;
	Buffer = netBufferPtr;

	CommRAM = Buffer;
	externalCommRAM = Buffer + 0x10000;

	m_attached = m_gameInfo.netboard_present && m_config["Network"].ValueAs<bool>();

	if (!m_attached)
		return Result::OKAY;

	if (IsGame("daytona2") || IsGame("harley") || IsGame("scud") || IsGame("srally2") ||
		IsGame("skichamp") || IsGame("spikeout") || IsGame("spikeofe"))
		m_gameType = GameType::one;
	else if (IsGame("lemans24") || IsGame("von2") || IsGame("dirtdvls"))
		m_gameType = GameType::two;
	else
		return ErrorLog("Game not recognized or supported");

	m_state = State::start;
	m_async = m_config["AsyncNet"].ValueAsDefault<bool>(false);
	InitializeGameNet();

	//netsocks
	port_in = m_config["PortIn"].ValueAs<unsigned>();
	port_out = m_config["PortOut"].ValueAs<unsigned>();
	addr_out = m_config["AddressOut"].ValueAs<std::string>();

	const auto centralRelay = m_config["NetRelay"].ValueAsDefault<std::string>("");
	const auto gameRelay = m_config["NetGameRelay"].ValueAsDefault<std::string>("");
	const auto relay = centralRelay.empty() ? std::string() : gameRelay.empty() ? centralRelay : gameRelay;
	m_relay = !relay.empty();
	m_video60 = m_relay && Online::RelayVideo60(m_config);
	if (m_video60) InfoLog("Relay: room runs 60 Hz emulated video (master option); frame timing uses 60 Hz instead of 57.524 Hz.");
	if (m_relay && (!m_gameNet.Enabled() ||
		!RelayProtocol::ParseEndpoint(relay,addr_out,port_out)))
		return ErrorLog("Relay requires a supported exact-ROM async HLE game and HOST:PORT.");
	const auto lobbyTicket = m_config["NetRelayTicket"].ValueAsDefault<std::string>("");
	uint8_t parsedTicket[16];
	if (!lobbyTicket.empty() && (!m_relay || !RelayProtocol::ParseTicket(lobbyTicket,parsedTicket)))
		return ErrorLog("Invalid online lobby ticket; reconnect through the Online tab.");
	if (m_relay) InfoLog("Relay: connecting to %s:%u; waiting for authorized lobby channels (no client listen port)",addr_out.c_str(),unsigned(port_out));

	if (!m_server)
		return ErrorLog("Net board requires the network server.");
	m_link = std::make_unique<CNetBoardLink>(m_server, addr_out, port_out, m_relay, m_gameNet.ProtocolGUID(asyncNetGUID), lobbyTicket);
	// Register handlers before starting the listener.
	if (m_gameNet.Enabled())
	{
		m_gameNet.m_linkInitTime = std::chrono::steady_clock::now();
		m_server->Start();
		// Connect in the background while ROM setup continues; no guest state is accessed here.
		m_connectThread = std::thread(&CSimNetBoard::ConnectProc, this);
	}

	return Result::OKAY;
}

void CSimNetBoard::RunFrame(void)
{
	if (!IsRunning())
		return;

	switch (m_state)
	{
	case State::start:
		if (!m_connected && !m_connectThread.joinable())
			m_connectThread = std::thread(&CSimNetBoard::ConnectProc, this);
		m_status0 = 0;
		m_status1 = IsGame("dirtdvls") ? 0x4004 : 0xe000;
		m_state = State::init;
		break;

	case State::init:
		memset(Buffer, 0, 0x20000); // clear from the allocation base, not the current bank
		if (m_gameType == GameType::one)
		{
			if (m_status0 & 0x8000)				// has main board changed this register?
			{
				m_IRQ2ack |= 0x01;				// simulate IRQ 2 ack
				if (m_status0 == 0xf000)
				{
					// initialization complete
					m_status1 = 0;
					CommRAM16[0x72] = FLIPENDIAN16(0x1); // is this necessary?
					m_state = State::testing;
				}
				m_status0 = 0;					// 0 should work for all init subroutines
			}
		}
		else
		{
			// type 2 performs initialization on its own
			m_status1 = 0;
			m_state = State::testing;
			m_counter = 0;
		}
		break;

	case State::testing:
		if (m_async)
		{
			RunAsyncTesting();
			break;
		}
		if (m_gameType == GameType::one)
		{
			m_status0 += 1; // type 1 games require this to be incremented every frame

			if (!m_connected)
				break;

			uint8_t numMachines, machineIndex;

			if (RAM16[0x400] == 0)	// master
			{
				// flush receive buffer
				while (m_link->CheckDataAvailable())
				{
					m_link->Receive();
				}

				// check all linked instances have the same GUID
				m_link->Send(&netGUID, sizeof(netGUID));
				auto& recv_data = m_link->Receive();
				if (recv_data.empty())
					break;
				uint64_t testGUID;
				memcpy(&testGUID, recv_data.data(), recv_data.size());
				if (testGUID != netGUID)
					testGUID = 0;

				// send the GUID for one more loop
				m_link->Send(&testGUID, sizeof(testGUID));
				m_link->Receive();
				
				if (testGUID != netGUID)
				{
					ErrorLog("unable to verify connection. Make sure all machines are using same build!");
					m_state = State::error;
					break;
				}

				// master has an index of zero
				machineIndex = 0;
				m_link->Send(&machineIndex, sizeof(machineIndex));

				// receive back the number of other linked machines
				recv_data = m_link->Receive();
				if (recv_data.empty())
					break;
				numMachines = recv_data[0];

				// send the number of other linked machines
				m_link->Send(&numMachines, sizeof(numMachines));
				m_link->Receive();
			}
			else
			{
				// receive GUID from the previous machine and check it matches
				auto& recv_data = m_link->Receive();
				if (recv_data.empty())
					break;
				uint64_t testGUID;
				memcpy(&testGUID, recv_data.data(), recv_data.size());
				if (testGUID != netGUID)
					testGUID = 0;
				m_link->Send(&testGUID, sizeof(testGUID));

				// one more time, in case a later machine has a GUID mismatch
				recv_data = m_link->Receive();
				if (recv_data.empty())
					break;
				memcpy(&testGUID, recv_data.data(), recv_data.size());
				if (testGUID != netGUID)
					testGUID = 0;
				m_link->Send(&testGUID, sizeof(testGUID));

				if (testGUID != netGUID)
				{
					ErrorLog("unable to verify connection. Make sure all machines are using same build!");
					m_state = State::error;
					break;
				}

				// receive the previous machine's index, increment it, send it to the next machine
				recv_data = m_link->Receive();
				if (recv_data.empty())
					break;
				machineIndex = recv_data[0] + 1;
				m_link->Send(&machineIndex, sizeof(machineIndex));

				// receive the number of other linked machines and forward it on
				recv_data = m_link->Receive();
				if (recv_data.empty())
					break;
				numMachines = recv_data[0];
				m_link->Send(&numMachines, sizeof(numMachines));
			}

			// if there are no other linked machines, only continue if Supermodel is linked to itself
			// there might be more than one machine set to master which would cause glitches
			if ((numMachines == 0) && ((port_in != port_out) || (addr_out != "127.0.0.1")))
			{
				ErrorLog("no slave machines detected. Make sure only one machine is set to master!");
				m_state = State::error;
				break;
			}

			m_numMachines = numMachines + 1;

			m_status0 = 0;		// supposed to cycle between 0 and 1 (also 2 for Daytona 2); doesn't seem to matter
			m_status1 = 0x2021 + (numMachines * 0x20) + machineIndex;

			CommRAM16[0x0] = RAM16[0x400];	// 0 if master, 1 if slave
			CommRAM16[0x2] = numMachines;
			CommRAM16[0x4] = machineIndex;

			m_counter = 0;
			CommRAM16[0x6] = 0;

			m_segmentSize = RAM16[0x404];

			// don't know if these are actually required, but it never hurts to include them
			CommRAM16[0x8] = FLIPENDIAN16(0x100 + m_segmentSize);
			CommRAM16[0xa] = FLIPENDIAN16(RAM16[0x402] - m_segmentSize - 1);
			CommRAM16[0xc] = FLIPENDIAN16(0x100);
			CommRAM16[0xe] = FLIPENDIAN16(RAM16[0x402] - m_segmentSize + 0x200);

			m_state = State::ready;
		}
		else
		{
			if (!m_connected)
				break;

			// we have to track both playable and non-playable machines for type 2
			struct
			{
				uint8_t total;
				uint8_t playable;
			} numMachines, machineIndex;

			if (RAM16[0x200] == 0)	// master
			{
				// flush receive buffer
				while (m_link->CheckDataAvailable())
					m_link->Receive();

				// check all linked instances have the same GUID
				m_link->Send(&netGUID, sizeof(netGUID));
				auto& recv_data = m_link->Receive();
				if (recv_data.empty())
					break;

				uint64_t testGUID;
				memcpy(&testGUID, recv_data.data(), recv_data.size());
				if (testGUID != netGUID)
					testGUID = 0;

				// send the GUID for one more loop
				m_link->Send(&testGUID, sizeof(testGUID));
				m_link->Receive();

				if (testGUID != netGUID)
				{
					ErrorLog("unable to verify connection. Make sure all machines are using same build!");
					m_state = State::error;
					break;
				}

				// master has indices set to zero
				machineIndex.total = 0; machineIndex.playable = 0;
				m_link->Send(&machineIndex, sizeof(machineIndex));

				// receive back the number of other linked machines
				recv_data = m_link->Receive();
				if (recv_data.empty())
					break;
				memcpy(&numMachines, recv_data.data(), recv_data.size());

				// send the number of other linked machines
				m_link->Send(&numMachines, sizeof(numMachines));
				m_link->Receive();
			}
			else if (RAM16[0x200] < 0x8000)	// slave
			{
				// receive GUID from the previous machine and check it matches
				auto& recv_data = m_link->Receive();
				if (recv_data.empty())
					break;
				uint64_t testGUID;
				memcpy(&testGUID, recv_data.data(), recv_data.size());
				if (testGUID != netGUID)
					testGUID = 0;
				m_link->Send(&testGUID, sizeof(testGUID));

				// one more time, in case a later machine has a GUID mismatch
				recv_data = m_link->Receive();
				if (recv_data.empty())
					break;
				memcpy(&testGUID, recv_data.data(), recv_data.size());
				if (testGUID != netGUID)
					testGUID = 0;
				m_link->Send(&testGUID, sizeof(testGUID));

				if (testGUID != netGUID)
				{
					ErrorLog("unable to verify connection. Make sure all machines are using same build!");
					m_state = State::error;
					break;
				}

				// receive the indices of the previous machine and increment them
				recv_data = m_link->Receive();
				if (recv_data.empty())
					break;
				memcpy(&machineIndex, recv_data.data(), recv_data.size());
				machineIndex.total++; machineIndex.playable++;

				// send our indices to the next machine
				m_link->Send(&machineIndex, sizeof(machineIndex));

				// receive the number of machines
				recv_data = m_link->Receive();
				if (recv_data.empty())
					break;
				memcpy(&numMachines, recv_data.data(), recv_data.size());

				// forward the number of machines
				m_link->Send(&numMachines, sizeof(numMachines));
			}
			else
			{
				// relay/satellite
				
				// receive GUID from the previous machine and check it matches
				auto& recv_data = m_link->Receive();
				if (recv_data.empty())
					break;
				uint64_t testGUID;
				memcpy(&testGUID, recv_data.data(), recv_data.size());
				if (testGUID != netGUID)
					testGUID = 0;
				m_link->Send(&testGUID, sizeof(testGUID));

				// one more time, in case a later machine has a GUID mismatch
				recv_data = m_link->Receive();
				if (recv_data.empty())
					break;
				memcpy(&testGUID, recv_data.data(), recv_data.size());
				if (testGUID != netGUID)
					testGUID = 0;
				m_link->Send(&testGUID, sizeof(testGUID));

				if (testGUID != netGUID)
				{
					ErrorLog("unable to verify connection. Make sure all machines are using same build!");
					m_state = State::error;
					break;
				}

				// receive the indices of the previous machine; don't increment the playable index
				recv_data = m_link->Receive();
				if (recv_data.empty())
					break;
				memcpy(&machineIndex, recv_data.data(), recv_data.size());
				machineIndex.total++;

				// send our indices to the next machine
				m_link->Send(&machineIndex, sizeof(machineIndex));

				// receive the number of machines
				recv_data = m_link->Receive();
				if (recv_data.empty())
					break;
				memcpy(&numMachines, recv_data.data(), recv_data.size());

				// forward the number of machines
				m_link->Send(&numMachines, sizeof(numMachines));

				// indicate that this machine is a relay/satellite
				if (!IsGame("dirtdvls"))
					machineIndex.playable |= 0x80;
			}

			// if there are no other linked machines, only continue if Supermodel is linked to itself
			// there might be more than one machine set to master which would cause glitches
			if ((numMachines.total == 0) && ((port_in != port_out) || (addr_out != "127.0.0.1")))
			{
				ErrorLog("no slave machines detected. Make sure only one machine is set to master!");
				if (IsGame("dirtdvls"))
					m_status1 = 0x8085;	// seems like the netboard code writers really liked their CPU model numbers
				m_state = State::error;
				break;
			}

			m_numMachines = numMachines.total + 1;

			m_status0 = 5;			// probably not necessary
			if (IsGame("dirtdvls"))
				m_status1 = (numMachines.playable << 4) | machineIndex.playable | 0x7400;
			else
				m_status1 = (numMachines.playable << 8) | machineIndex.playable;

			CommRAM16[0x0] = RAM16[0x200];	// master/slave/relay status
			CommRAM16[0x2] = (numMachines.playable << 8) | numMachines.total;
			CommRAM16[0x4] = (machineIndex.playable << 8) | machineIndex.total;

			m_counter = 0;
			CommRAM16[0x6] = 0;

			m_segmentSize = RAM16[0x204];

			// don't know if these are actually required, but it never hurts to include them
			CommRAM16[0x8] = FLIPENDIAN16(0x100 + m_segmentSize);
			CommRAM16[0xa] = FLIPENDIAN16(RAM16[0x206]);
			CommRAM16[0xc] = FLIPENDIAN16(0x100);
			CommRAM16[0xe] = FLIPENDIAN16(RAM16[0x206] + 0x80);

			m_state = State::ready;
		}
		break;

	case State::ready:
		m_counter++;
		CommRAM16[0x6] = FLIPENDIAN16(m_counter);
		
		if (m_async)
			RunAsyncExchange();
		else
		{
			// we only send what we need to; helps cut down on bandwidth
			// each machine has to receive back its own data (TODO: copy this data manually?)
			for (int i = 0; i < m_numMachines; i++)
			{
				m_link->Send(CommRAM + 0x100 + i * m_segmentSize, m_segmentSize);
				auto& recv_data = m_link->Receive();
				if (recv_data.empty())
				{
					// link broken - send an "empty" packet to alert other machines
					m_link->Send(nullptr, 0);
					m_state = State::error;
					if (m_gameType == GameType::one)
						m_status1 = 0x40;			// send "link broken" message to mainboard
					break;
				}
				memcpy(CommRAM + 0x100 + (i + 1) * m_segmentSize, recv_data.data(), recv_data.size());
			}
		}

		// swap CommRAM banks; the bank the game gets next starts a new write record
		if (m_commbank)
		{
			m_commbank = false;
			CommRAM = Buffer;
			externalCommRAM = Buffer + 0x10000;
			m_bankWriteFirstUs[1] = m_bankWriteLastUs[1] = 0;
		}
		else
		{
			m_commbank = true;
			CommRAM = Buffer + 0x10000;
			externalCommRAM = Buffer;
			m_bankWriteFirstUs[0] = m_bankWriteLastUs[0] = 0;
		}
		
		break;

	case State::error:
		// do nothing
		break;
	}
}

// These routines only poll the transport and copy bounded, complete snapshots.
// There is deliberately no extra HLE worker touching RAM or register state.
void CSimNetBoard::AsyncError(const char* reason)
{
	m_onlineFailed.store(true);
	ErrorLog("Async net board: %s", reason);
	m_link->DisableEarlyForward();
	m_link->Send(nullptr, 0); // propagate failure around the ring once
	m_state = State::error;
	m_gameNet.OnFailure();
	m_asyncInFlight = false;
	m_asyncInFlightCount = 0;
	m_asyncUnackedFromPeer = 0;
	if (m_gameType == GameType::one)
		m_status1 = 0x40;
	else if (IsGame("dirtdvls"))
		m_status1 = 0x8085;
}

bool CSimNetBoard::AsyncTimedOut()
{
	// Wall time, not emulated frames: fast-forward must not cause false timeouts.
	if (std::chrono::steady_clock::now() - m_asyncProgress < std::chrono::seconds(30))
		return false;
	AsyncError("no link progress for 30 seconds; restart all linked instances");
	return true;
}

void CSimNetBoard::RunAsyncTesting()
{
	if (m_gameType == GameType::one)
		m_status0++; // firmware heartbeat must continue during network setup
	if (!m_connected || (m_relay && !m_relayNativeReady))
		return;

	const bool typeOne = m_gameType == GameType::one;
	const size_t countSize = typeOne ? 1 : 2;
	if (m_asyncPhase == AsyncPhase::start)
	{
		const uint16_t role = RAM16[typeOne ? 0x400 : 0x200];
		const unsigned relayRole = m_relay ? (m_gameInfo.name == "srally2" ? m_link->RelayIndex() : (m_link->RelayIndex()==0 ? 0u : 1u)) : 0u;
		if (m_relay && role != relayRole)
		{ char detail[160]; snprintf(detail,sizeof(detail),"native relay role mismatch: expected %u, received %u",relayRole,unsigned(role)); AsyncError(detail); return; }
		m_asyncMaster = role == 0;
		m_asyncRelay = !typeOne && role >= 0x8000;
		m_asyncIndex[0] = m_asyncIndex[1] = 0;
		m_asyncCount[0] = m_asyncCount[1] = 0;
		m_asyncGUID = m_gameNet.AsyncProtocolGUID(asyncNetGUID);
		m_asyncProgress = std::chrono::steady_clock::now();
		if (m_asyncMaster)
			m_link->Send(&m_asyncGUID, sizeof(m_asyncGUID));
		m_asyncPhase = AsyncPhase::guid;
	}

	// At most four handshake messages per emulated frame, even on loopback.
	for (unsigned budget = 0; budget < 4; budget++)
	{
		std::vector<uint8_t> data;
		const auto result = m_link->TryReceive(data);
		if (result == CNetBoardLink::ReceiveResult::Pending)
		{
			AsyncTimedOut();
			return;
		}
		if (result == CNetBoardLink::ReceiveResult::Closed)
		{
			AsyncError("link closed during setup");
			return;
		}
		m_asyncProgress = std::chrono::steady_clock::now();
		const bool guidPhase = m_asyncPhase == AsyncPhase::guid || m_asyncPhase == AsyncPhase::verify;
		if (data.size() != (guidPhase ? sizeof(m_asyncGUID) : countSize))
		{
			AsyncError("invalid setup packet size (all peers must use the same mode/build)");
			return;
		}

		switch (m_asyncPhase)
		{
		case AsyncPhase::guid:
			memcpy(&m_asyncGUID, data.data(), sizeof(m_asyncGUID));
			if (m_asyncGUID != (m_gameNet.AsyncProtocolGUID(asyncNetGUID)))
				m_asyncGUID = 0;
			m_link->Send(&m_asyncGUID, sizeof(m_asyncGUID));
			// Ordered TCP permits pipelining while the GUID is verified.
			if (m_asyncMaster && m_gameNet.Enabled() && m_asyncGUID)
				m_link->Send(m_asyncIndex, countSize);
			m_asyncPhase = AsyncPhase::verify;
			break;

		case AsyncPhase::verify:
			memcpy(&m_asyncGUID, data.data(), sizeof(m_asyncGUID));
			if (m_asyncGUID != (m_gameNet.AsyncProtocolGUID(asyncNetGUID)))
				m_asyncGUID = 0;
			if (!m_asyncMaster)
				m_link->Send(&m_asyncGUID, sizeof(m_asyncGUID));
			if (!m_asyncGUID)
			{
				AsyncError("mode/build mismatch; AsyncNet, exact ROM and entry-sync protocol must match on all peers");
				return;
			}
			if (m_asyncMaster && !m_gameNet.Enabled())
				m_link->Send(m_asyncIndex, countSize);
			m_asyncPhase = AsyncPhase::index;
			break;

		case AsyncPhase::index:
			if (m_asyncMaster)
			{
				memcpy(m_asyncCount, data.data(), countSize);
				m_link->Send(m_asyncCount, countSize);
			}
			else
			{
				memcpy(m_asyncIndex, data.data(), countSize);
				if (m_asyncIndex[0] == 255 || (!typeOne && m_asyncIndex[1] == 255))
				{
					AsyncError("too many linked machines");
					return;
				}
				m_asyncIndex[0]++;
				if (!typeOne && !m_asyncRelay)
					m_asyncIndex[1]++;
				m_link->Send(m_asyncIndex, countSize);
			}
			m_asyncPhase = AsyncPhase::count;
			break;

		case AsyncPhase::count:
			if (!m_asyncMaster)
			{
				memcpy(m_asyncCount, data.data(), countSize);
				m_link->Send(m_asyncCount, countSize);
			}
			else if (memcmp(m_asyncCount, data.data(), countSize) != 0)
			{
				AsyncError("inconsistent machine count");
				return;
			}
			FinishAsyncTesting();
			return;

		case AsyncPhase::start:
			return; // handled above
		}
	}
}

void CSimNetBoard::FinishAsyncTesting()
{
	const bool typeOne = m_gameType == GameType::one;
	const unsigned total = m_asyncCount[0]; // number of OTHER machines
	if ((!total && (port_in != port_out || addr_out != "127.0.0.1")) ||
		total == 255 || m_asyncIndex[0] > total ||
		(!typeOne && (m_asyncCount[1] > total || m_asyncIndex[1] > m_asyncCount[1])))
	{
		AsyncError("invalid machine count; configure exactly one master in the ring");
		return;
	}

	if (m_relay && (total+1 != m_link->RelayCount() || m_asyncIndex[0] != m_link->RelayIndex()))
	{ AsyncError("relay roster does not match verified native ring"); return; }
	if (m_relay && m_link)
		SetTickRate(m_link->RelayTickRate());
	m_numMachines = uint8_t(total + 1);
	m_segmentSize = RAM16[typeOne ? 0x404 : 0x204];
	// Local segment plus one segment per machine must fit in ONE CommRAM bank.
	if (!m_segmentSize || 0x100u + (unsigned(m_numMachines) + 1) * m_segmentSize > 0x10000u)
	{
		AsyncError("invalid segment size or too many machines for CommRAM");
		return;
	}

	if (typeOne)
	{
		m_status0 = 0;
		m_status1 = 0x2021 + total * 0x20 + m_asyncIndex[0];
		CommRAM16[0x0] = RAM16[0x400];
		CommRAM16[0x2] = total;
		CommRAM16[0x4] = m_asyncIndex[0];
		CommRAM16[0xa] = FLIPENDIAN16(RAM16[0x402] - m_segmentSize - 1);
		CommRAM16[0xe] = FLIPENDIAN16(RAM16[0x402] - m_segmentSize + 0x200);
	}
	else
	{
		uint8_t playableIndex = m_asyncIndex[1];
		if (m_asyncRelay && !IsGame("dirtdvls"))
			playableIndex |= 0x80;
		m_status0 = 5;
		m_status1 = IsGame("dirtdvls") ?
			(m_asyncCount[1] << 4) | playableIndex | 0x7400 :
			(m_asyncCount[1] << 8) | playableIndex;
		CommRAM16[0x0] = RAM16[0x200];
		CommRAM16[0x2] = (m_asyncCount[1] << 8) | total;
		CommRAM16[0x4] = (playableIndex << 8) | m_asyncIndex[0];
		CommRAM16[0xa] = FLIPENDIAN16(RAM16[0x206]);
		CommRAM16[0xe] = FLIPENDIAN16(RAM16[0x206] + 0x80);
	}
	m_counter = 0;
	CommRAM16[0x6] = 0;
	CommRAM16[0x8] = FLIPENDIAN16(0x100 + m_segmentSize);
	CommRAM16[0xc] = FLIPENDIAN16(0x100);

	m_asyncWorking.assign(size_t(m_numMachines) * m_segmentSize, 0);
	m_asyncLatest = m_asyncWorking;
	m_asyncWorkingAgeUs.assign(m_numMachines, 0);
	m_asyncWorkingUncertaintyUs.assign(m_numMachines, Dayto2pe::MaxTimingUncertaintyUs);
	m_asyncWorkingReceiveUs.assign(m_numMachines, 0);
	m_asyncLatestAgeUs = m_asyncWorkingAgeUs;
	m_asyncLatestUncertaintyUs = m_asyncWorkingUncertaintyUs;
	m_asyncLatestSourceUs.assign(m_numMachines, 0);
	m_asyncKeyframeSourceUs.assign(m_numMachines, 0);
	m_asyncLastTcpSourceUs = 0;
	m_asyncKeyframe.assign(size_t(m_numMachines) * m_segmentSize, 0);
	m_asyncKeyframeValid.assign(m_numMachines, 0);
	m_udpReadySinceUs = 0;
	m_udpPrimary = false;
	m_udpByteActivity.clear();
	m_udpStreamed.clear();
	m_udpClassSample.clear();
	m_udpClassNextUs = 0;
	m_asyncLastClockPingUs = 0;
	m_asyncInFlightCount = 0;
	m_asyncUnackedFromPeer = 0;
	m_tickPhase = FrameMilliHz();
	m_asyncLastSendUs = 0;
	m_asyncLastSentSourceUs = 0;
	m_asyncSendFrames.clear();
	m_asyncLastSent.clear();
	m_asyncProgress = std::chrono::steady_clock::now();
	if (!ConfigureGameNet()) return;
	m_asyncInFlight = false;
	// Timed-segment games forward peer hops from the receive thread as soon as
	// they arrive. Forwarding is not game-visible: what Sega Rally 2's barriers
	// need is that the game sees every segment of every slot, which the
	// publication rule in the receive loop below guarantees (one segment per
	// slot per frame, the returning self echo closing the round). When the
	// relay fans segments out itself (RLS1) nothing is forwarded: every
	// segment arrives straight from the relay with the hop count of its slot.
	if (m_gameNet.UseTimedSegments())
	{
		const bool star = m_relay && m_link->RelayStar();
		m_link->EnableEarlyForward(m_segmentSize, m_numMachines, !star);
		InfoLog("Async net board: %s (%u machines, %u-byte segments)",
			star ? "relay star fan-out, no ring forwarding" : "early forwarding of ring segments enabled", m_numMachines, unsigned(m_segmentSize));
	}
	else
		m_link->DisableEarlyForward();
	m_state = State::ready;
}

void CSimNetBoard::SetTickRate(unsigned hz, bool midSession)
{
	const unsigned clamped = std::clamp(hz, 5u, 60u);
	if (m_tickRateHz != clamped)
	{
		m_tickRateHz = clamped;
		if (!midSession)
			m_gameNet.nativePacing.Reset();
		m_tickPhase = FrameMilliHz();
		m_gameNet.SetTickRate(m_tickRateHz);
		InfoLog(midSession ? "Async net board: update tick rate changed to %u Hz by the host (congestion control)"
		                   : "Async net board: update tick rate set to %u Hz", m_tickRateHz);
	}
}

// One monotonic stamp sequence orders this cabinet's snapshots on both lanes.
uint64_t CSimNetBoard::NextSourceStampUs(uint64_t nowUs)
{
	m_asyncLastSentSourceUs = nowUs > m_asyncLastSentSourceUs ? nowUs : m_asyncLastSentSourceUs + 1;
	return m_asyncLastSentSourceUs;
}

void CSimNetBoard::RunAsyncExchange()
{
	if (m_relay && m_link)
	{
		const unsigned linkHz = m_link->RelayTickRate();
		if (linkHz != m_tickRateHz)
			SetTickRate(linkHz, m_link->RelayAdaptiveTick());
	}
	// One latest-wins UDP snapshot per tick: streamed state (positions), never
	// resent. Each snapshot names the TCP control message it was built on
	// (controlUs) and receivers hold it until that message has arrived. A
	// snapshot that already contains a control change not yet sent by TCP names
	// itself, so receivers never apply it: the TCP message that follows carries
	// that change. It still counts as a sign of life for the UDP lane.
	if (m_relay && m_link && m_link->UdpActive())
	{
		const uint64_t udpNow = AsyncNowUs();
		if (udpNow >= m_udpNextSendUs)
		{
			const uint64_t interval = 1000000ULL / m_tickRateHz;
			// A peer counts as heard for 3.5 ticks (at least 150 ms) after its last snapshot.
			const uint64_t stampUs = NextSourceStampUs(udpNow);
			const std::vector<uint8_t> mask = StreamedMask();
			uint64_t sampleAgeUs = 0, sampleUncertaintyUs = 0;
			SampleTiming(udpNow, sampleAgeUs, sampleUncertaintyUs);
			m_link->SendPresentation(CommRAM + 0x100, m_segmentSize, stampUs,
				std::max<uint64_t>(150000ULL, interval * 7 / 2),
				(m_udpPrimary && ControlMessagePending()) ? stampUs : m_asyncLastTcpSourceUs,
				mask.empty() ? nullptr : &mask, sampleAgeUs, sampleUncertaintyUs);
			m_udpNextSendUs = m_udpNextSendUs && udpNow - m_udpNextSendUs < interval
				? m_udpNextSendUs + interval : udpNow + interval;
		}
	}
	TickStartSync();
	if (m_state == State::error) return;
	m_gameNet.OnFrame();
	const unsigned frameMilliHz = FrameMilliHz();
	const uint64_t nowUs = AsyncNowUs();
	const unsigned exchangeHz = NativeExchangeTickRate(nowUs);
	if(exchangeHz != m_exchangeRateHz) {
		m_exchangeRateHz=exchangeHz;
		m_tickPhase=frameMilliHz; // Prompt first exchange, no accumulated catch-up burst.
	}
	m_tickPhase = Online::AccumulateTick(m_tickPhase, exchangeHz, frameMilliHz);
	const bool tickDue = (m_tickPhase >= frameMilliHz);
	if (m_gameNet.UseTimedSegments())
	{
		// Refresh the direct-neighbour clock for sample-age estimates.
		// While UDP carries the race the ring is silent: no originated segments
		// and no hop-clock pings (TickStartSync keeps its 1 Hz keepalive ping).
		ClassifyOutgoing(nowUs);
		const bool udpSilent = UdpPrimarySilent(nowUs);
		const int64_t clockRttUs = m_link->GetIncomingClock().rtt;
		const uint64_t clockPingIntervalUs = std::max<uint64_t>(500000ULL, uint64_t(std::max<int64_t>(0, clockRttUs)));
		if (!udpSilent && (nowUs < m_asyncLastClockPingUs || nowUs - m_asyncLastClockPingUs >= clockPingIntervalUs))
		{
			m_link->RefreshIncomingClock();
			m_asyncLastClockPingUs = nowUs;
		}
		// Pace ring snapshots with bounded queues; forward peer hops immediately.
		constexpr unsigned MaxInFlight = 48;
		constexpr unsigned MaxUnackedFromPeer = 30;
		constexpr uint64_t SendPacingUs = 14500;
		const uint64_t minSpacingUs = exchangeHz >= 60u ? SendPacingUs : (850000ULL / exchangeHz);
		const bool spacingReady = m_asyncLastSendUs == 0 ||
			(nowUs >= m_asyncLastSendUs && (nowUs - m_asyncLastSendUs) >= minSpacingUs);
		const bool idle = !m_asyncInFlight && m_asyncInFlightCount == 0;
		const bool idleReady = idle && tickDue && (exchangeHz >= 60u || !m_relay || spacingReady);
		const bool payloadChanged = m_asyncLastSent.size() != m_segmentSize ||
			memcmp(CommRAM + 0x100, m_asyncLastSent.data(), m_segmentSize) != 0;
		const bool canPipeline = m_numMachines > 1 && !m_gameNet.m_startWaiting &&
			payloadChanged && tickDue && m_asyncInFlightCount < MaxInFlight &&
			m_asyncUnackedFromPeer < MaxUnackedFromPeer && spacingReady;
		if (!udpSilent && (idleReady || canPipeline))
		{
			m_tickPhase = Online::ConsumeTick(m_tickPhase, frameMilliHz);
			const uint64_t sourceStampUs = NextSourceStampUs(nowUs);
			m_asyncLastTcpSourceUs = sourceStampUs;
			++m_statTcpOriginated;
			if (m_udpPrimary) NoteControlMessage();
			uint64_t sampleAgeUs = 0, sampleUncertaintyUs = 0;
			SampleTiming(nowUs, sampleAgeUs, sampleUncertaintyUs);
			auto wire = Dayto2pe::MakeTimedSegment(CommRAM + 0x100, m_segmentSize, nowUs, sampleUncertaintyUs, sourceStampUs, sampleAgeUs);
			m_link->Send(wire.data(), wire.size());
			m_asyncLastSent.assign(CommRAM + 0x100, CommRAM + 0x100 + m_segmentSize);
			// Candidate delta base for UDP snapshots; it is used once its echo returns.
			if (m_relay) m_link->NoteUdpLocalKeyframe(sourceStampUs, m_asyncLastSent.data(), m_segmentSize);
			m_asyncLastSendUs = nowUs;
			m_asyncSendFrames.push_back(m_gameNet.m_predictionFrame);
			if (idle)
			{
				m_gameNet.OnExchangeStarted();
				m_asyncProgress = std::chrono::steady_clock::now();
			}
			++m_asyncInFlightCount;
			++m_asyncUnackedFromPeer;
			m_asyncInFlight = true;
		}

		const unsigned maxBudget = 8u * unsigned(m_numMachines) + 16u + m_gameNet.ControlBudget();
		uint32_t publishedSlots = 0; // slots given a segment this frame
		for (unsigned budget = 0; budget < maxBudget; budget++)
		{
			// Sega Rally 2 publication rule: a slot gets at most one segment per
			// frame, so a value held for a single segment (its barriers pulse
			// acknowledgements that way) is never overwritten before the game has
			// seen it. The next segment waits in the queue for the next frame; the
			// receive thread has already forwarded it, so peers are not delayed.
			// A long backlog (more than three rounds) is drained freely to catch up.
			if (m_gameNet.m_preserveRingSelfEcho && m_link->QueueSize() <= 3u * size_t(m_numMachines))
			{
				uint32_t nextSlot = 0;
				if (m_link->PeekNextSlot(m_segmentSize, nextSlot) && nextSlot < 32 && (publishedSlots & (1u << nextSlot)))
					break;
			}
			std::vector<uint8_t> data;
			CNetBoardLink::PacketInfo info;
			const auto result = m_link->TryReceive(data, info);
			if (result == CNetBoardLink::ReceiveResult::Pending)
			{
				if (AsyncTimedOut())
					return;
				break;
			}
			if (result == CNetBoardLink::ReceiveResult::Data && m_gameNet.IsControlPacket(data.size()))
			{
				ReceiveStartToken(data);
				if (m_state == State::error) return;
				continue; // control token is NOT a ROM segment
			}

			uint32_t incomingHops = 0;
			bool validHops = result == CNetBoardLink::ReceiveResult::Data &&
				Dayto2pe::ReadTimedSegmentHops(data, m_segmentSize, incomingHops);
			if (validHops && info.advanced)
			{
				// The receive thread already counted this hop (hops+1) when it forwarded.
				if (incomingHops == 0) validHops = false; else --incomingHops;
			}
			if (!validHops || incomingHops >= m_numMachines)
			{
				char detail[160];
				snprintf(detail, sizeof(detail),
					"link broken or invalid timed-segment size (received=%zu expected=%zu segment=%u result=%d hops=%u)",
					data.size(), sizeof(Dayto2pe::TimedSegmentHeader) + m_segmentSize, unsigned(m_segmentSize),
					int(result), unsigned(incomingHops));
				AsyncError(detail);
				return;
			}

			const uint64_t measurementNowUs = AsyncNowUs();
			if (!info.advanced)
			{
				const auto clock = m_link->GetIncomingClock();
				const uint64_t clockAge = clock.received <= measurementNowUs ? measurementNowUs - clock.received : UINT64_MAX;
				if (!Dayto2pe::AdvanceTimedSegment(data, m_segmentSize, measurementNowUs, clock.offset,
					clock.rtt, clockAge))
				{
					AsyncError("invalid timed-segment metadata");
					return;
				}
			}
			const uint8_t* payload = nullptr;
			uint64_t ageUs = 0, uncertaintyUs = Dayto2pe::MaxTimingUncertaintyUs;
			if (!Dayto2pe::ReadTimedSegment(data, m_segmentSize, payload, ageUs, uncertaintyUs))
			{
				AsyncError("invalid timed-segment payload");
				return;
			}
			// An early-forwarded segment was aged up to its arrival; add the time it
			// then waited in the queue for this frame.
			if (info.advanced && measurementNowUs > info.receivedUs)
				ageUs = std::min<uint64_t>(10000000, ageUs + (measurementNowUs - info.receivedUs));

			m_asyncProgress = std::chrono::steady_clock::now();
			m_asyncUnackedFromPeer = 0;

			// Forward peer segments without waiting for the self-echo (unless the
			// receive thread already did, or the relay fans segments out itself).
			if (!info.advanced && incomingHops + 1 < m_numMachines && !(m_relay && m_link->RelayStar()))
				m_link->Send(data.data(), data.size());

			const size_t offset = size_t(incomingHops) * m_segmentSize;
			uint64_t sourceUs = 0;
			Dayto2pe::ReadTimedSegmentSourceUs(data, m_segmentSize, sourceUs);
			// Every TCP segment is the reliable race-context reference for its slot.
			if (m_asyncKeyframe.size() >= offset + m_segmentSize)
			{
				memcpy(m_asyncKeyframe.data() + offset, payload, m_segmentSize);
				m_asyncKeyframeValid[incomingHops] = 1;
				m_asyncKeyframeSourceUs[incomingHops] = sourceUs;
				// The same bytes undo that cabinet's UDP deltas built on this segment.
				if (m_relay && incomingHops + 1 < m_numMachines)
					m_link->SetUdpPeerBase(UdpOriginForSlot(incomingHops), sourceUs, payload, m_segmentSize);
			}
			// A newer UDP snapshot may already fill this slot; never step back in time.
			if (sourceUs >= m_asyncLatestSourceUs[incomingHops] || incomingHops + 1 == m_numMachines)
			{
				memcpy(m_asyncLatest.data() + offset, payload, m_segmentSize);
				m_asyncLatestAgeUs[incomingHops] = ageUs;
				m_asyncLatestUncertaintyUs[incomingHops] = uncertaintyUs;
				m_asyncLatestSourceUs[incomingHops] = sourceUs;
			}
			else
			{
				// A newer UDP snapshot was applied with this slot's previous control
				// words (streamed words only). Take the control words from this
				// reliable segment so no control value is skipped.
				++m_statTcpStaleSkipped;
				MergeControlWords(incomingHops, payload);
			}
			m_asyncWorkingReceiveUs[incomingHops] = measurementNowUs;
			if (incomingHops < 32) publishedSlots |= 1u << incomingHops;

			if (incomingHops + 1 == m_numMachines)
			{
				// Our own segment is back: every cabinet holds it, so UDP deltas may use it.
				if (m_relay) m_link->CommitUdpLocalKeyframe(sourceUs);
				if (measurementNowUs >= sourceUs) { m_statRingRttSumUs += measurementNowUs - sourceUs; ++m_statRingRttCount; }
				if (m_relay && measurementNowUs - m_linkStatsLogUs >= 10000000)
				{
					// Periodic link summary for real-ROM sessions (ring RTT is send stamp -> self-echo).
					if (m_linkStatsLogUs)
						InfoLog("Async net board: link stats: %s, %llu ring round trips, mean RTT %.1f ms, %llu segments forwarded by the receive thread, predictor inputs from UDP %llu (%llu timed), UDP snapshots applied to CommRAM %llu",
							m_link->RelayStar() ? "relay star fan-out" : "ring", (unsigned long long)m_statRingRttCount,
							m_statRingRttCount ? double(m_statRingRttSumUs) / double(m_statRingRttCount) / 1000.0 : 0.0,
							(unsigned long long)m_link->EarlyForwarded(), (unsigned long long)m_statUdpPresentation,
							(unsigned long long)m_statUdpPresentationTimed, (unsigned long long)m_statUdpApplied);
					m_linkStatsLogUs = measurementNowUs;
				}
				if (!m_asyncSendFrames.empty())
				{
					m_gameNet.m_asyncRoundStartFrame = m_asyncSendFrames.front();
					m_asyncSendFrames.pop_front();
				}
				if (m_asyncInFlightCount > 0)
					--m_asyncInFlightCount;
				m_asyncInFlight = m_asyncInFlightCount > 0;
				RecordGameSnapshot();
				if (m_gameNet.m_preserveRingSelfEcho &&
					m_link->QueueSize() <= 3u * size_t(m_numMachines))
				{
					// Preserve round-consistent slots for Sega Rally 2 barriers.
					break;
				}
			}
		}
		if (m_state == State::error) return;
		ApplyUdpSnapshots(AsyncNowUs());
	}
	else
	{
		if (!m_asyncInFlight && tickDue)
		{
			m_tickPhase = Online::ConsumeTick(m_tickPhase, frameMilliHz);
			m_link->Send(CommRAM + 0x100, m_segmentSize);
			m_gameNet.OnExchangeStarted();
			m_asyncReceived = 0;
			m_asyncInFlight = true;
			m_asyncProgress = std::chrono::steady_clock::now();
		}

		// Bound work per frame. In particular, never spin waiting for a packet.
		for (unsigned budget = 0; budget < unsigned(m_numMachines) + m_gameNet.ControlBudget(); budget++)
		{
			std::vector<uint8_t> data;
			const auto result = m_link->TryReceive(data);
			if (result == CNetBoardLink::ReceiveResult::Pending)
			{
				if (AsyncTimedOut())
					return;
				break;
			}
			if (result == CNetBoardLink::ReceiveResult::Data && m_gameNet.IsControlPacket(data.size()))
			{
				ReceiveStartToken(data);
				if (m_state == State::error) return;
				continue;
			}
			if (result == CNetBoardLink::ReceiveResult::Closed || data.size() != m_segmentSize)
			{
				char detail[160];
				snprintf(detail, sizeof(detail),
					"link broken or invalid segment size (received=%zu expected=%zu result=%d slot=%u)",
					data.size(), size_t(m_segmentSize), int(result), unsigned(m_asyncReceived));
				AsyncError(detail);
				return;
			}

			m_asyncProgress = std::chrono::steady_clock::now();
			const size_t offset = size_t(m_asyncReceived) * m_segmentSize;
			memcpy(m_asyncWorking.data() + offset, data.data(), m_segmentSize);
			if (++m_asyncReceived == m_numMachines)
			{
				m_asyncWorking.swap(m_asyncLatest);
				RecordGameSnapshot();
				m_asyncInFlight = false;
				break;
			}
			m_link->Send(data.data(), data.size()); // raw ring segment
		}
	}

	// Refresh both receive banks before the bank swap.
	memcpy(CommRAM + 0x100 + m_segmentSize, m_asyncLatest.data(), m_asyncLatest.size());
	ApplyGamePrediction();
	// Preserve the native self-echo for games with ring barriers.
	if (m_gameNet.ImmediateSelfEcho())
		memcpy(CommRAM + 0x100 + size_t(m_numMachines) * m_segmentSize,
			CommRAM + 0x100, m_segmentSize);
}

void CSimNetBoard::Reset(void)
{
	// if netboard was active, send an "empty" packet so the other machines don't get stuck waiting for data
	if (m_state == State::ready ||
		(m_async && m_state == State::testing && m_asyncPhase != AsyncPhase::start))
	{
		InfoLog("Async net board: Reset during %s/phase %u sends the empty flush packet around the ring",
			m_state == State::ready ? "ready" : "testing", unsigned(m_asyncPhase));
		m_link->Send(nullptr, 0);
		if (!m_async)
			m_link->Receive();
	}

	m_udpNextSendUs = 0;
	m_udpLogged = false;
	m_udpReadySinceUs = 0;
	m_udpPrimary = false;
	m_udpByteActivity.clear();
	m_udpStreamed.clear();
	m_udpClassSample.clear();
	m_udpClassNextUs = 0;
	m_asyncKeyframe.clear();
	m_asyncKeyframeValid.clear();
	if (m_link) m_link->DisableEarlyForward();
	if (m_relay && m_link) m_link->ClearUdpBases();
	m_asyncPhase = AsyncPhase::start;
	m_asyncInFlight = false;
	m_asyncInFlightCount = 0;
	m_asyncUnackedFromPeer = 0;
	m_tickPhase = FrameMilliHz();
	m_asyncLastSendUs = 0;
	m_asyncLastSentSourceUs = 0;
	m_asyncSendFrames.clear();
	m_asyncReceived = 0;
	ResetStartSync();
	m_asyncLastSent.clear();
	m_asyncWorking.clear();
	m_asyncLatest.clear();
	m_asyncLatestSourceUs.clear();
	m_asyncKeyframeSourceUs.clear();
	m_asyncLastTcpSourceUs = 0;
	m_gameNet.ResetPresentation();
	if (m_async && Buffer)
	{
		m_commbank = false;
		CommRAM = Buffer;
		externalCommRAM = Buffer + 0x10000;
	}
	m_running = false;
	m_state = State::start;
}

bool CSimNetBoard::IsAttached(void)
{
	return m_attached;
}

bool CSimNetBoard::IsRunning(void)
{
	return m_attached && m_running;
}

void CSimNetBoard::GetGame(const Game& gameInfo)
{
	m_gameInfo = gameInfo;
	// Emulate the busy/complete sequence expected by Sega Rally 2 link status.
	m_commandPort = (gameInfo.name == "srally2" || gameInfo.name == "srally2p" ||
		gameInfo.name == "srally2pa" || gameInfo.name == "srally2dx");
	m_commandPoll = 0;
}

void CSimNetBoard::ConnectProc(void)
{
	if (m_connected)
		return;

	printf("Connecting to %s:%i ..\n", addr_out.c_str(), port_out);

	// wait until the output connection to the next machine is open
	while (!m_link->Connect())
	{
		if (m_quit || (m_relay && m_link->RelayIndex()==-2))
			return;
		std::this_thread::sleep_for(std::chrono::milliseconds(20)); // do not spin if a peer has not started yet
	}

	// wait until the previous machine has linked
	while (!m_link->Connected())
	{
		if (m_quit || (m_relay && m_link->RelayIndex()==-2))
			return;
		// Retry during initial setup if a proxy accepts before its destination is ready.
		m_link->Connect();
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}

	printf("Successfully connected.\n");
	LogGameTransportReady();

	m_connected = true;
}

uint8_t CSimNetBoard::ReadCommRAM8(unsigned addr)
{
	return externalCommRAM[addr];
}

uint16_t CSimNetBoard::ReadCommRAM16(unsigned addr)
{
	return *(uint16_t*)&externalCommRAM[addr];
}

uint32_t CSimNetBoard::ReadCommRAM32(unsigned addr)
{
	return *(uint32_t*)&externalCommRAM[addr];
}

void CSimNetBoard::WriteCommRAM8(unsigned addr, uint8_t data)
{
	externalCommRAM[addr] = data;
	NoteSegmentWrite(addr);
}

void CSimNetBoard::WriteCommRAM16(unsigned addr, uint16_t data)
{
	*(uint16_t*)&externalCommRAM[addr] = data;
	NoteSegmentWrite(addr);
}

void CSimNetBoard::WriteCommRAM32(unsigned addr, uint32_t data)
{
	*(uint32_t*)&externalCommRAM[addr] = data;
	NoteSegmentWrite(addr);
}

void CSimNetBoard::NoteSegmentWrite(unsigned addr)
{
	if (!m_async || m_state != State::ready || !m_segmentSize ||
		addr < 0x100 || addr >= 0x100 + unsigned(m_segmentSize)) return;
	const unsigned bank = externalCommRAM == Buffer ? 0 : 1;
	const uint64_t now = AsyncNowUs();
	if (!m_bankWriteFirstUs[bank]) m_bankWriteFirstUs[bank] = now;
	m_bankWriteLastUs[bank] = now;
}

// Age and uncertainty of the local segment about to be sent from CommRAM. The
// game wrote it while this bank was external, i.e. before the previous bank
// swap; the write times make that one to two frames explicit instead of
// stamping the pick-up time with a whole frame of uncertainty. Without a
// write in the last few frames (unchanged data, or writes that bypass the
// hooks) the old conservative stamp is used.
void CSimNetBoard::SampleTiming(uint64_t nowUs, uint64_t& ageUs, uint64_t& uncertaintyUs) const
{
	const unsigned bank = CommRAM == Buffer ? 0 : 1;
	const uint64_t first = m_bankWriteFirstUs[bank], last = m_bankWriteLastUs[bank];
	const uint64_t frameUs = uint64_t(FrameDurationUs() + 0.5);
	if (last && first && first <= last && nowUs >= last && nowUs - last <= 4 * frameUs)
	{
		ageUs = nowUs - last;
		uncertaintyUs = (last - first) + 1000; // write burst span plus a scheduling allowance
	}
	else
	{
		ageUs = 0;
		uncertaintyUs = frameUs;
	}
}

uint16_t CSimNetBoard::ReadIORegister(unsigned reg)
{
	if (!IsRunning())
		return 0;

	switch (reg)
	{
	case 0x00:
		return m_IRQ2ack;
	case 0x88:
		if (m_commandPort)
		{
			if (m_commandPoll < 2) { ++m_commandPoll; return uint16_t(m_status0 | 0x8000); }
			return 0;
		}
		return m_status0;
	case 0x8a:
		return m_status1;
	default:
		ErrorLog("read from unknown IO register 0x%02x", reg);
		return 0;
	}
}

void CSimNetBoard::WriteIORegister(unsigned reg, uint16_t data)
{
	switch (reg)
	{
	case 0x00:
		m_IRQ2ack = data;
		break;
	case 0x88:
		m_status0 = data;
		if (m_commandPort)
		{
			InfoLog("srally2 link board: command %04x", data);
			m_commandPoll = 0;
		}
		break;
	case 0x8a:
		m_status1 = data;
		break;
	case 0xc0:
		if (data == 0)
			Reset();
		m_running = (data != 0);
		break;
	default:
		ErrorLog("write to unknown IO register 0x%02x", reg);
	}
}
