// Experimental native link identity advertised by capable lobby/relay builds.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_ONLINE_SGT24H_H
#define INCLUDED_ONLINE_SGT24H_H
#include "Network/Games/Sgt24h/Profile.h"
namespace Sgt24hOnline {
inline constexpr unsigned Game=12;
inline constexpr float RefreshRate=16000000.0f/(656*424);
inline constexpr const char* Rom="sgt24h";
inline constexpr const char* Title="Super GT 24h";
inline constexpr uint64_t Guid=0x5bf177da34881ULL;
inline constexpr unsigned Cabinets=Sgt24h::Cabinets;
inline constexpr const char* PoseHello="Supermodel SGT24H SGP1";
inline constexpr unsigned PoseWireBytes=960;
inline constexpr unsigned PayloadSize=Sgt24h::FrameBytes+1;
}
#endif
