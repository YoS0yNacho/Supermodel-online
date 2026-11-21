// INDY 500 Twin (Rev A) Online contract. Native Model 2 link timing.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_ONLINE_INDY500_H
#define INCLUDED_ONLINE_INDY500_H
#include <cstddef>
#include <cstdint>

namespace Indy500Online {
inline constexpr unsigned Game = 9;  // appended to the stable Online lobby IDs
inline constexpr const char* Rom = "indy500";
inline constexpr unsigned Cabinets = 8;  // eight driving seats (four Twin sets)
inline constexpr uint64_t Guid = 0x5bf177da3487eULL;
// Optional board-channel HEL1 text (the RLJ2 admission itself is unchanged).
// Old relays ignore this text; old clients never receive a UDP config.
inline constexpr const char* PoseHello = "Supermodel INDY500 IPU1";
inline constexpr const char* RaceHello = "Supermodel INDY500 IRU1";
inline constexpr unsigned PoseSlots = 8;
inline constexpr size_t PoseWireBytes = 20 + PoseSlots * 12;
// FF (link ID), FE (link size), FC (vblank) and race data are all padded to
// the same native board size, including the type/cabinet byte.
inline constexpr size_t PayloadSize = 0x0e00 + 1;
inline constexpr size_t RaceWireBytes = 20 + PoseSlots * 20;
inline constexpr size_t RacePairHeaderBytes = Cabinets + 1;
inline constexpr size_t RacePairMinBytes = RacePairHeaderBytes + 2;
inline constexpr size_t RacePairMaxBytes = RacePairHeaderBytes + PayloadSize + 1;
// Model 2 video timing (see LINE_TIME and TOTAL_LINES in Model2.cpp).
inline constexpr float RefreshRate = 16000000.0f / (656 * 424);
// HEL1 text advertises this optional game without changing the OEH6 ROM mask
// that old directories and Model 3 creator-hosted rooms validate strictly.
inline constexpr const char* ClientHello = "Supermodel Online (indy500)";
inline constexpr const char* RelayHello = "Supermodel Online lobby v1 (indy500)";
// A separate greeting advertises Indy native settings, not the Model 3
// native-capability bit that older Indy clients already send.
inline constexpr const char* RoomClientHello = "Supermodel Online (indy500 room options)";
inline constexpr const char* RoomRelayHello = "Supermodel Online lobby v1 (indy500 room options)";
}
#endif
