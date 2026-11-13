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
 * NetBoardLink.h
 *
 * Header file for the CNetBoardLink class: the network connections of a net
 * board (CNetBoard or CSimNetBoard).
 *
 * Linked machines make a ring. Each machine sends data to the next machine
 * and receives data from the previous machine. The output connection goes
 * from this machine to the CNetServer of the next machine. The input
 * connection comes from the previous machine to the CNetServer of this
 * machine. One machine can be its own next and previous machine.
 *
 * Messages
 * --------
 *    Join    The previous machine sends it when the output connection opens.
 *            Thus, this machine knows which connection is the input.
 *    Data    One block of net board data. An empty block tells the other
 *            machines that the link is broken.
 *
 * Threads
 * -------
 * Receive() waits until data arrives or the link is broken. The net boards
 * normally operate in lock step. Async HLE uses TryReceive() instead, leaving
 * emulation free to advance while a ring exchange is pending. The CNetServer
 * thread receives messages and puts them in the same queue for both APIs.
 */

#ifndef INCLUDED_NETBOARDLINK_H
#define INCLUDED_NETBOARDLINK_H

#include "Network/NetServer.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class CRelayUdpClient;

class CNetBoardLink
{
public:
  /*
   * Connect(void):
   *
   * Tries one time to open the output connection to the next machine.
   *
   * Returns:
   *    True if the output connection is open.
   */
  bool Connect(void);

  // True if the input and the output connections are open
  bool Connected(void);
  // -1: waiting for operator; -2: failed; otherwise immutable relay index.
  int RelayIndex();
  unsigned RelayCount();
  unsigned RelayTickRate() const { return m_relayTickRate.load(std::memory_order_acquire); }
  bool IsRelay() const { return m_relay; }
  // UDP snapshot lane (relay sessions only). sourceUs orders this cabinet's
  // snapshots across both lanes; freshUs is the peer-silence tolerance.
  struct UdpSnapshot { std::vector<uint8_t> raw; uint64_t sourceUs=0, receivedUs=0, ageUs=0, uncertaintyUs=0, controlUs=0; bool timed=false; };
  void SendPresentation(const void*,size_t,uint64_t sourceUs,uint64_t freshUs,uint64_t controlUs=0);
  bool LatestPresentation(unsigned origin,std::vector<uint8_t>& raw,uint64_t& sourceUs,uint64_t& receivedUs);
  // Latest snapshot from origin, only if strictly newer than afterSourceUs.
  bool LatestSnapshot(unsigned origin,uint64_t afterSourceUs,UdpSnapshot& out);
  bool UdpActive();
  // Every other cabinet sends usable UDP snapshots and confirms receiving ours.
  bool UdpPrimaryReady();

  /*
   * Send(data, length):
   *
   * Sends a block of data to the next machine. Does not wait. A length of
   * zero tells the other machines that the link is broken.
   */
  void Send(const void *data, size_t length);

  /*
   * Receive(void):
   *
   * Waits for a block of data from the previous machine.
   *
   * Returns:
   *    The data. Empty if the link is broken (or if the previous machine sent
   *    an empty block). The reference is valid until the next call.
   */
  std::vector<uint8_t> &Receive(void);

  // Never waits for network data. Pending leaves data unchanged; Closed also
  // covers an empty (link-broken) packet. Only one thread may consume data.
  enum class ReceiveResult { Pending, Data, Closed };
  ReceiveResult TryReceive(std::vector<uint8_t> &data);

  // True if Receive() can return data immediately
  bool CheckDataAvailable(void);
  size_t QueueSize(void);

  CTCPConnection::ClockSample GetIncomingClock();
  void RefreshIncomingClock();

  /*
   * CNetBoardLink(server, address, port):
   *
   * Parameters:
   *    server    Network server of this machine.
   *    address   Address of the next machine.
   *    port      Port of the next machine.
   */
  CNetBoardLink(std::shared_ptr<CNetServer> server, const std::string &address, uint16_t port, bool relay = false, uint64_t relayGUID = 0, const std::string& lobbyTicket = "");
  ~CNetBoardLink(void);

private:
  bool ConnectRelay();
  bool OnUdpConfig(const std::shared_ptr<CTCPConnection>&,const uint8_t*,size_t);
  std::unique_ptr<CRelayUdpClient> m_udp;
  CNetServer::Registration m_udpHandler, m_udpHostHandler;
  bool OnRelayTickRate(const std::shared_ptr<CTCPConnection>&, const uint8_t*, size_t);
  bool OnRelayReady(const std::shared_ptr<CTCPConnection>&, const uint8_t*, size_t);
  bool OnRelayError(const std::shared_ptr<CTCPConnection>&, const uint8_t*, size_t);
  bool LinkBroken(void) const;
  bool OnJoin(const std::shared_ptr<CTCPConnection> &connection);
  bool OnData(const std::shared_ptr<CTCPConnection> &connection, const uint8_t *message, size_t size);
  void OnDisconnect(const std::shared_ptr<CTCPConnection> &connection);

  std::shared_ptr<CNetServer> m_server;
  const std::string m_address;
  const uint16_t m_port;
  const bool m_relay;
  const uint64_t m_relayGUID;
  const std::string m_lobbyTicket;
  uint64_t m_relayNonce[2] = {};
  bool m_relayAssigned[2] = {};
  bool m_relayTickRateSeen[2] = {};
  bool m_relayFailed = false;
  unsigned m_relayIndex = 0, m_relayCount = 0;
  std::atomic<unsigned> m_relayTickRate{60};

  std::mutex m_mutex;
  std::condition_variable m_condition;
  std::shared_ptr<CTCPConnection> m_output;
  std::shared_ptr<CTCPConnection> m_input;
  std::deque<std::vector<uint8_t>> m_queue;
  std::vector<uint8_t> m_received;      // last block that Receive() returned

  CNetServer::Registration m_relayTickRateHandler, m_relayReadyHandler, m_relayErrorHandler;
  CNetServer::Registration m_joinHandler;
  CNetServer::Registration m_dataHandler;
  CNetServer::Registration m_disconnectHandler;
};

#endif  // INCLUDED_NETBOARDLINK_H
