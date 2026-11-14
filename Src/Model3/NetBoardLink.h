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
  // True once the host announced (RLT2) that it adapts the tick rate during the
  // session; RelayTickRate() then follows its mid-session RLT1 messages.
  bool RelayAdaptiveTick() const { return m_relayAdaptive.load(std::memory_order_acquire); }
  unsigned RelayTickCap() const { return m_relayTickCap.load(std::memory_order_acquire); }
  // True once the relay announced star fan-out (RLS1) for this session.
  bool RelayStar() const { return m_relayStar.load(std::memory_order_acquire); }
  bool IsRelay() const { return m_relay; }
  // UDP snapshot lane (relay sessions only). sourceUs orders this cabinet's
  // snapshots across both lanes; freshUs is the peer-silence tolerance.
  // mask: the origin's streamed-word mask (bit w = segment word w is UDP-only), empty if none received yet.
  // ageUs: age of the data at receivedUs = transitUs (stamp -> arrival) + the sender's sample age.
  struct UdpSnapshot { std::vector<uint8_t> raw, mask; uint64_t sourceUs=0, receivedUs=0, ageUs=0, transitUs=0, uncertaintyUs=0, controlUs=0; bool timed=false; };
  void SendPresentation(const void*,size_t,uint64_t sourceUs,uint64_t freshUs,uint64_t controlUs=0,const std::vector<uint8_t>* streamedMask=nullptr,
                        uint64_t sampleAgeUs=0,uint64_t sampleUncertaintyUs=0);
  // Latest snapshot from origin, only if strictly newer than afterSourceUs.
  bool LatestSnapshot(unsigned origin,uint64_t afterSourceUs,UdpSnapshot& out);
  // Delta bases for the UDP lane: segments this cabinet originated by TCP
  // (a candidate becomes the base when its self-echo returns) and the last TCP
  // segments received from each peer (its keyframes).
  void NoteUdpLocalKeyframe(uint64_t sourceUs,const void* bytes,size_t size);
  void CommitUdpLocalKeyframe(uint64_t sourceUs);
  void SetUdpPeerBase(unsigned origin,uint64_t sourceUs,const void* bytes,size_t size);
  void ClearUdpBases();
  struct UdpStats { uint64_t sent=0,sentDelta=0,sentBytes=0,sentRawBytes=0,received=0,receivedDelta=0,heldResolved=0,deltaStale=0,deltaBaseUnknown=0,maskSent=0,maskReceived=0,
                    datagrams=0,lost=0,reordered=0,duplicated=0; }; // see RelayUdpClient::Stats
  bool GetUdpStats(UdpStats& out);
  bool UdpActive();
  // Every other cabinet sends usable UDP snapshots and confirms receiving ours.
  bool UdpPrimaryReady(bool holding=false);

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
  // advanced: the receive thread already advanced this timed segment's header
  // (hops+1, age up to receivedUs) and forwarded it to the next machine; the
  // consumer must not advance or forward it again (see EnableEarlyForward).
  struct PacketInfo { bool advanced = false; uint64_t receivedUs = 0; };
  ReceiveResult TryReceive(std::vector<uint8_t> &data, PacketInfo &info);
  // Early forwarding: valid timed segments of `segmentSize` bytes in a ring of
  // `machines` are advanced and sent on to the next machine by the receive
  // thread as soon as they arrive, instead of waiting for the emulation
  // thread's next frame. Each hop then costs network time only. The copy
  // queued for the consumer is flagged `advanced`. Forwarding touches no guest
  // RAM; the consumer still publishes the payload at its own pace.
  // forward=false (relay star fan-out): segments are still advanced and
  // registered as UDP delta bases on arrival, but never sent on.
  void EnableEarlyForward(size_t segmentSize, unsigned machines, bool forward = true);
  void DisableEarlyForward();
  uint64_t EarlyForwarded() const { return m_earlyForwarded.load(std::memory_order_relaxed); }

  // True if Receive() can return data immediately
  bool CheckDataAvailable(void);
  size_t QueueSize(void);
  // Slot (hop count as TryReceive's consumer will see it) of the next queued
  // packet, if it is a timed segment of segmentSize; false otherwise.
  bool PeekNextSlot(size_t segmentSize, uint32_t& slot);

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
  bool OnRelayAdaptiveTick(const std::shared_ptr<CTCPConnection>&, const uint8_t*, size_t);
  bool OnRelayStar(const std::shared_ptr<CTCPConnection>&, const uint8_t*, size_t);
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
  bool m_relayStarSeen[2] = {};
  std::atomic<bool> m_relayStar{false};
  bool m_relayFailed = false;
  unsigned m_relayIndex = 0, m_relayCount = 0;
  std::atomic<unsigned> m_relayTickRate{60}, m_relayTickCap{60};
  std::atomic<bool> m_relayAdaptive{false};

  std::mutex m_mutex;
  std::condition_variable m_condition;
  std::shared_ptr<CTCPConnection> m_output;
  std::shared_ptr<CTCPConnection> m_input;
  struct Packet { std::vector<uint8_t> data; PacketInfo info; };
  std::deque<Packet> m_queue;
  std::vector<uint8_t> m_received;      // last block that Receive() returned
  size_t m_earlySegmentSize = 0;        // 0 = early forwarding off
  unsigned m_earlyMachines = 0;
  bool m_earlySend = true;              // false: advance on arrival only (star fan-out)
  std::atomic<uint64_t> m_earlyForwarded{0};

  CNetServer::Registration m_relayTickRateHandler, m_relayAdaptiveHandler, m_relayStarHandler, m_relayReadyHandler, m_relayErrorHandler;
  CNetServer::Registration m_joinHandler;
  CNetServer::Registration m_dataHandler;
  CNetServer::Registration m_disconnectHandler;
};

#endif  // INCLUDED_NETBOARDLINK_H
