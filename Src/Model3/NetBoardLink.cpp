/**
 ** Supermodel
 ** A Sega Model 3 Arcade Emulator.
 ** Copyright 2003-2026 The Supermodel Team
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
 * NetBoardLink.cpp
 *
 * Implementation of the CNetBoardLink class. Refer to NetBoardLink.h.
 */

#include "NetBoardLink.h"

#include "OSD/Logger.h"
#include <cstring>
#include <random>
#include "Network/RelayProtocol.h"
#include "Network/RelayUdpClient.h"
#include "Network/Games/Policy.h"

namespace NetBoardProtocol
{
#pragma pack(push, 1)

  struct JoinMessage: public NetProtocol::MessageHeader
  {
    static constexpr uint32_t ID = NetProtocol::ID("NBJ1");

    JoinMessage()
      : MessageHeader(ID, sizeof(JoinMessage))
    {
    }
  };

  static_assert(sizeof(JoinMessage) == 8, "JoinMessage must be 8 bytes");

  // Then follows the block of data
  struct DataMessage: public NetProtocol::MessageHeader
  {
    static constexpr uint32_t ID = NetProtocol::ID("NBD1");

    DataMessage(size_t dataLength = 0)
      : MessageHeader(ID, sizeof(DataMessage) + dataLength)
    {
    }
  };

  static_assert(sizeof(DataMessage) == 8, "DataMessage must be 8 bytes");

#pragma pack(pop)
}

using namespace NetBoardProtocol;


/******************************************************************************
 Send and Receive
******************************************************************************/

void CNetBoardLink::Send(const void *data, size_t length)
{
  std::shared_ptr<CTCPConnection> output;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    output = m_output;
  }
  if (!output)
    return;
  DataMessage header(length);
  std::vector<uint8_t> message(header.size);
  memcpy(message.data(), &header, sizeof(header));
  if (length > 0)
    memcpy(message.data() + sizeof(header), data, length);
  output->Send(std::move(message));
}

// Caller must hold m_mutex
bool CNetBoardLink::LinkBroken(void) const
{
  return !m_input || (m_output && !m_output->IsOpen());
}

std::vector<uint8_t> &CNetBoardLink::Receive(void)
{
  std::unique_lock<std::mutex> lock(m_mutex);
  m_condition.wait(lock, [this] { return !m_queue.empty() || LinkBroken(); });
  if (m_queue.empty())
    m_received.clear();
  else
  {
    m_received = std::move(m_queue.front());
    m_queue.pop_front();
  }
  return m_received;
}

CNetBoardLink::ReceiveResult CNetBoardLink::TryReceive(std::vector<uint8_t> &data)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_queue.empty())
  {
    if (!LinkBroken())
      return ReceiveResult::Pending;
    ErrorLog("Net board: link reported broken (input=%d inputOpen=%d outputOpen=%d) - treating as closed",
      m_input ? 1 : 0, (m_input && m_input->IsOpen()) ? 1 : 0, (m_output && m_output->IsOpen()) ? 1 : 0);
    return ReceiveResult::Closed;
  }
  data = std::move(m_queue.front());
  m_queue.pop_front();
  return data.empty() ? ReceiveResult::Closed : ReceiveResult::Data;
}

CTCPConnection::ClockSample CNetBoardLink::GetIncomingClock()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_input ? m_input->GetClockSample() : CTCPConnection::ClockSample();
}

void CNetBoardLink::RefreshIncomingClock()
{
  std::shared_ptr<CTCPConnection> input;
  { std::lock_guard<std::mutex> lock(m_mutex); input = m_input; }
  if (input) input->Ping();
}

bool CNetBoardLink::CheckDataAvailable(void)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return !m_queue.empty();
}

size_t CNetBoardLink::QueueSize(void)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_queue.size();
}


/******************************************************************************
 Connections
******************************************************************************/

bool CNetBoardLink::Connect(void)
{
  if (m_relay) return ConnectRelay();
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_output && m_output->IsOpen())
      return true;
  }
  std::shared_ptr<CTCPConnection> output = m_server->Connect(m_address, m_port);
  if (!output)
    return false;
  output->Send(JoinMessage());
  std::lock_guard<std::mutex> lock(m_mutex);
  m_output = output;
  return true;
}

bool CNetBoardLink::Connected(void)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_input && m_input->IsOpen() && m_output && m_output->IsOpen() &&
    (!m_relay || (!m_relayFailed && m_relayAssigned[0] && m_relayAssigned[1]));
}

bool CNetBoardLink::OnJoin(const std::shared_ptr<CTCPConnection> &connection)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_input && m_input != connection && m_input->IsOpen())
  {
    ErrorLog("Net board: %s tried to link, but %s is the previous machine. Make sure that each machine has a different next machine.", connection->GetPeerName().c_str(), m_input->GetPeerName().c_str());
    return false;
  }
  m_input = connection;
  m_condition.notify_all();
  return true;
}

bool CNetBoardLink::OnData(const std::shared_ptr<CTCPConnection> &connection, const uint8_t *message, size_t size)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (connection != m_input)
  {
    ErrorLog("Net board: %s sent data before it linked.", connection->GetPeerName().c_str());
    return false;
  }
  m_queue.emplace_back(message + sizeof(DataMessage), message + size);
  m_condition.notify_all();
  return true;
}

void CNetBoardLink::OnDisconnect(const std::shared_ptr<CTCPConnection> &connection)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_relay && (connection == m_input || connection == m_output))
  {
    m_relayFailed = true;
    ErrorLog("Relay: connection lost; restart all clients and create a new Online lobby.");
  }
  if (connection == m_input)
    m_input.reset();
  m_condition.notify_all();   // also for the output connection
}

CNetBoardLink::CNetBoardLink(std::shared_ptr<CNetServer> server, const std::string &address, uint16_t port, bool relay, uint64_t relayGUID, const std::string& lobbyTicket)
  : m_server(std::move(server)),
    m_address(address),
    m_port(port), m_relay(relay), m_relayGUID(relayGUID), m_lobbyTicket(lobbyTicket)
{
  if (m_relay)
  {
    std::random_device random;
    for (auto& nonce : m_relayNonce) nonce = (uint64_t(random()) << 32) | random();
    m_udpHandler = m_server->RegisterHandler(RelayProtocol::UdpConfig::ID, [this](const auto& c,const uint8_t* p,size_t n) { return OnUdpConfig(c,p,n); });
    m_udpHostHandler = m_server->RegisterHandler(RelayProtocol::UdpHostConfigID, [this](const auto& c,const uint8_t* p,size_t n) { return OnUdpConfig(c,p,n); });
    m_relayTickRateHandler = m_server->RegisterHandler(RelayProtocol::TickRate::ID, [this](const auto& c, const uint8_t* p, size_t n) { return OnRelayTickRate(c,p,n); });
    m_relayReadyHandler = m_server->RegisterHandler(RelayProtocol::Ready::ID, [this](const auto& c, const uint8_t* p, size_t n) { return OnRelayReady(c,p,n); });
    m_relayErrorHandler = m_server->RegisterHandler(RelayProtocol::Error::ID, [this](const auto& c, const uint8_t* p, size_t n) { return OnRelayError(c,p,n); });
  }
  m_joinHandler = m_server->RegisterHandler(JoinMessage::ID, [this](const std::shared_ptr<CTCPConnection> &connection, const uint8_t *, size_t size)
  {
    return size == sizeof(JoinMessage) && OnJoin(connection);
  });
  m_dataHandler = m_server->RegisterHandler(DataMessage::ID, [this](const std::shared_ptr<CTCPConnection> &connection, const uint8_t *message, size_t size)
  {
    return OnData(connection, message, size);
  });
  m_disconnectHandler = m_server->RegisterDisconnectHandler([this](const std::shared_ptr<CTCPConnection> &connection)
  {
    OnDisconnect(connection);
  });
}

CNetBoardLink::~CNetBoardLink(void)
{
  m_udpHandler.Reset();
  m_udpHostHandler.Reset();
  m_relayTickRateHandler.Reset();
  m_relayReadyHandler.Reset();
  m_relayErrorHandler.Reset();
  m_joinHandler.Reset();
  m_dataHandler.Reset();
  m_disconnectHandler.Reset();
  m_udp.reset(); // all callbacks are quiescent before destroying the UDP worker
  if (m_output)
    m_output->Close();
  if (m_relay && m_input) m_input->Close();
}


// Two outbound sockets preserve the existing bidirectional peer clock paths:
// local channel 0 <-> next cabinet channel 1, with opaque framed forwarding.
bool CNetBoardLink::ConnectRelay()
{
  for (unsigned channel=0; channel<2; ++channel)
  {
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      if (m_relayFailed) return false;
      const auto& c = channel ? m_input : m_output;
      if (c && c->IsOpen()) continue;
    }
    auto c = m_server->Connect(m_address,m_port);
    if (!c) return false;
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      (channel ? m_input : m_output) = c;
    }
    RelayProtocol::Join join;
    join.channel=channel;
    join.guid=m_relayGUID;
    memcpy(join.nonce,m_relayNonce,sizeof(join.nonce));
    if (m_lobbyTicket.empty()) { if (!c->Send(join)) return false; }
    else {
      RelayProtocol::HybridJoin online;
      online.channel=join.channel;online.guid=join.guid;
      memcpy(online.nonce,join.nonce,sizeof(join.nonce));
      if (!RelayProtocol::ParseTicket(m_lobbyTicket,online.ticket) || !c->Send(online)) return false;
    }
  }
  return true;
}
int CNetBoardLink::RelayIndex()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_relayFailed) return -2;
  return m_relayAssigned[0] && m_relayAssigned[1] ? int(m_relayIndex) : -1;
}
unsigned CNetBoardLink::RelayCount()
{
  std::lock_guard<std::mutex> lock(m_mutex); return m_relayCount;
}
bool CNetBoardLink::OnRelayTickRate(const std::shared_ptr<CTCPConnection>& c, const uint8_t* p, size_t size)
{
  if (size != sizeof(RelayProtocol::TickRate)) return false;
  RelayProtocol::TickRate info; memcpy(&info, p, size);
  std::lock_guard<std::mutex> lock(m_mutex);
  const unsigned channel = c==m_output ? 0 : c==m_input ? 1 : 2;
  if (info.version != RelayProtocol::Version ||
      info.tickRate < RelayProtocol::MinTickRate || info.tickRate > RelayProtocol::MaxTickRate ||
      channel > 1 || m_relayAssigned[channel] || m_relayTickRateSeen[channel] || m_relayFailed) return false;
  const unsigned other = channel ^ 1;
  if (m_relayTickRateSeen[other] && m_relayTickRate.load(std::memory_order_relaxed) != info.tickRate) return false;
  m_relayTickRate.store(info.tickRate, std::memory_order_release);
  m_relayTickRateSeen[channel] = true;
  return true;
}
bool CNetBoardLink::OnRelayReady(const std::shared_ptr<CTCPConnection>& c, const uint8_t* p, size_t size)
{
  if (size != sizeof(RelayProtocol::Ready)) return false;
  RelayProtocol::Ready ready; memcpy(&ready,p,size);
  std::lock_guard<std::mutex> lock(m_mutex);
  const unsigned channel = c==m_output ? 0 : c==m_input ? 1 : 2;
  unsigned maxPlayers = m_relayGUID==Srally2Research::Guid?8u:0u;
  for (const char* game : {"daytona2", "dayto2pe", "scud", "scudplus", "srally2", "lemans24"})
    if (GameNetwork::GUID(game) == m_relayGUID) maxPlayers = GameNetwork::MaxCabinets(game);
  if (ready.version!=RelayProtocol::Version || ready.count<2 || ready.count>maxPlayers || ready.index>=ready.count ||
      channel>1 || ready.channel!=channel || m_relayAssigned[channel] || m_relayFailed ||
      (m_udp && (m_udp->Index()!=ready.index || m_udp->Count()!=ready.count)) ||
      (m_relayCount && (m_relayCount!=ready.count || m_relayIndex!=ready.index))) return false;
  m_relayIndex=ready.index; m_relayCount=ready.count; m_relayAssigned[channel]=true;
  if (m_relayAssigned[0] && m_relayAssigned[1])
    InfoLog("Relay: room started, cabinet %u/%u, native %s / car %u, tick rate %u Hz",
      ready.index+1,ready.count,ready.index==0?"MASTER":"SLAVE",ready.index+1,m_relayTickRate.load(std::memory_order_relaxed));
  m_condition.notify_all(); return true;
}
bool CNetBoardLink::OnRelayError(const std::shared_ptr<CTCPConnection>& c, const uint8_t* p, size_t size)
{
  if(size!=sizeof(RelayProtocol::Error)) return false;
  RelayProtocol::Error error;memcpy(&error,p,size);
  std::lock_guard<std::mutex> lock(m_mutex);
  if(c!=m_input && c!=m_output) return false;
  m_relayFailed=true;
  if (error.code==6) ErrorLog("Relay: direct/manual-start connections are no longer supported; update Supermodel and open the Online GUI.");
  else ErrorLog("Relay: rejected (code %u); restart clients and create a new Online lobby.",error.code);
  c->Close(); // valid relay error: close deliberately, not a malformed protocol message
  m_condition.notify_all();return true;
}


bool CNetBoardLink::OnUdpConfig(const std::shared_ptr<CTCPConnection>& c,const uint8_t* p,size_t n)
{
 if(n<sizeof(RelayProtocol::UdpConfig)) return false;
 RelayProtocol::UdpConfig cfg;std::memcpy(&cfg,p,sizeof(cfg));
 const bool hosted=cfg.id==RelayProtocol::UdpHostConfigID;
 if(!hosted && n!=sizeof(cfg)) return false;
 std::lock_guard<std::mutex> lock(m_mutex);
 if(c!=m_output || m_udp || m_relayFailed || m_relayAssigned[0] || cfg.version!=1 || !cfg.port || cfg.port>65535 || cfg.count<2 || cfg.count>16 || cfg.index>=cfg.count) return false;
 bool nonzero=false;for(auto b:cfg.token) nonzero|=b!=0;if(!nonzero) return false;
 size_t size=m_relayGUID==Srally2Research::Guid?2048u:0u;
 if(m_relayGUID==Srally2Research::Guid && cfg.count>8)return false;
 for(const char* game:{"daytona2","dayto2pe","scud","scudplus","srally2","lemans24"})
  if(GameNetwork::GUID(game)==m_relayGUID) { size=GameNetwork::PacketSize(game);if(cfg.count>GameNetwork::MaxCabinets(game)) return false; }
 if(!size) return false;
 std::string host;uint16_t unused;
 if(!RelayProtocol::ParseEndpoint(c->GetPeerName(),host,unused)) return false;
 RelayUdp::HostConfig hc;
 if(hosted) {
  if(n<56) return false;
  uint32_t port=0,tick=0,count=0;std::memcpy(&hc.ip,p+40,4);std::memcpy(&port,p+44,4);std::memcpy(&tick,p+48,4);std::memcpy(&count,p+52,4);
  if(!hc.ip || port<1024 || port>65535 || tick<5 || tick>60 || count!=(cfg.index==0?cfg.count:0) || n!=56+20*count) return false;
  hc.port=uint16_t(port);hc.tickRate=tick;
  for(unsigned i=0;i<count;++i) { RelayUdp::HostPeer peer;std::memcpy(&peer.ip,p+56+20*i,4);std::memcpy(peer.token.data(),p+60+20*i,16);hc.peers.push_back(peer); }
  if(count && std::memcmp(hc.peers[0].token.data(),cfg.token,16)) return false;
 }
 m_udp=std::make_unique<CRelayUdpClient>(host,uint16_t(cfg.port),cfg.token,cfg.index,cfg.count,size,hosted?&hc:nullptr);
 InfoLog("Relay UDP: snapshot lane configured (UDP port %u); in-race snapshots use UDP, setup/start control and fallback use TCP",cfg.port);
 return true;
}
void CNetBoardLink::SendPresentation(const void* p,size_t n,uint64_t sourceUs,uint64_t freshUs,uint64_t controlUs)
{
 std::lock_guard<std::mutex> lock(m_mutex);if(m_udp && !m_relayFailed) m_udp->Submit(p,n,sourceUs,freshUs,controlUs);
}
bool CNetBoardLink::LatestSnapshot(unsigned origin,uint64_t afterSourceUs,UdpSnapshot& out)
{
 std::lock_guard<std::mutex> lock(m_mutex);
 CRelayUdpClient::Sample s;if(!m_udp || m_relayFailed || !m_udp->Latest(origin,s,afterSourceUs)) return false;
 out.raw=std::move(s.raw);out.sourceUs=s.sourceUs;out.receivedUs=s.receivedUs;out.ageUs=s.ageUs;out.uncertaintyUs=s.uncertaintyUs;out.controlUs=s.controlUs;out.timed=s.timed;
 return true;
}
bool CNetBoardLink::UdpPrimaryReady()
{
 std::lock_guard<std::mutex> lock(m_mutex);return m_udp && !m_relayFailed && m_udp->PrimaryReady();
}
bool CNetBoardLink::LatestPresentation(unsigned origin,std::vector<uint8_t>& raw,uint64_t& sourceUs,uint64_t& receivedUs)
{
 std::lock_guard<std::mutex> lock(m_mutex);
 CRelayUdpClient::Sample s;if(!m_udp || m_relayFailed || !m_udp->Latest(origin,s)) return false;
 raw=std::move(s.raw);sourceUs=s.sourceUs;receivedUs=s.receivedUs;return true;
}
bool CNetBoardLink::UdpActive()
{
 std::lock_guard<std::mutex> lock(m_mutex);return m_udp && !m_relayFailed && m_udp->Active();
}
