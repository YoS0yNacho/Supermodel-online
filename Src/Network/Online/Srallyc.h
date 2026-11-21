// Sega Rally Championship Twin/DX Revision C: Model 2A native ring.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_ONLINE_SRALLYC_H
#define INCLUDED_ONLINE_SRALLYC_H
#include <cstddef>
#include <cstdint>

namespace SrallycOnline {
inline constexpr unsigned Game = 10; // append; never reuse or renumber existing IDs
inline constexpr const char* Rom = "srallyc";
inline constexpr const char* Title = "Sega Rally Championship (Rev C)";
inline constexpr unsigned Cabinets = 4; // CAR 1..4; NOTLINK and RELAY are not seats
inline constexpr uint64_t Guid = 0x5bf177da3487fULL;
inline constexpr size_t PayloadSize = 0x0e00 + 1;
// Optional board HEL1; room discovery/launch capability and RLJ2 are unchanged.
inline constexpr const char* PoseHello = "Supermodel SRALLYC SRP1";
inline constexpr size_t PoseWireBytes = 240;
inline constexpr float RefreshRate = 16000000.0f / (656 * 424);
// OEH6 remains Model 3-only. These greetings advertise the exact ROM and
// mandatory support for its native room settings, without changing old IDs.
inline constexpr const char* ClientHello = "Supermodel Online (srallyc)";
inline constexpr const char* RelayHello = "Supermodel Online lobby v1 (srallyc)";
inline constexpr const char* BothClientHello = "Supermodel Online (indy500 room options, srallyc)";
inline constexpr const char* BothRelayHello = "Supermodel Online lobby v1 (indy500 room options, srallyc)";
}
#endif
