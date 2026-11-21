// Daytona USA, original Sega Model 2 Revision A (not Daytona USA 2).
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_ONLINE_DAYTONA_H
#define INCLUDED_ONLINE_DAYTONA_H
#include <cstddef>
#include <cstdint>
namespace DaytonaOnline {
inline constexpr unsigned Game = 11;
inline constexpr const char* Rom = "daytona";
inline constexpr const char* Title = "Daytona USA (Rev A)";
inline constexpr unsigned Cabinets = 8;
inline constexpr uint32_t ProgramCRC = 0xf8d20cf1u;
inline constexpr uint64_t Guid = 0x5bf177da34880ULL;
inline constexpr size_t PayloadSize = 0x0e00 + 1;
inline constexpr const char* PoseHello = "Supermodel DAYTONA DYP1";
inline constexpr size_t PoseWireBytes = 224;
inline constexpr float RefreshRate = 16000000.0f / (656 * 424);
// An exact digit 0..3 advertises the other installed Model 2 titles:
// bit 0 = Indy room options, bit 1 = srallyc. OEH6 remains Model 3-only.
inline constexpr const char* ClientHello = "Supermodel Online DAYTONA1 ";
inline constexpr const char* RelayHello = "Supermodel Online lobby v1 DAYTONA1 ";
}
#endif
