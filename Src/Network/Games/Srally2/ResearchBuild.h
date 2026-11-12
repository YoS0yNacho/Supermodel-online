// Both exact-ROM Rally variants are available in the same client.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SRALLY2_RESEARCH_BUILD_H
#define SRALLY2_RESEARCH_BUILD_H
#include <cstdint>
namespace Srally2Research {
inline constexpr bool Enabled=true; // compiled capability, NOT runtime selection
inline constexpr unsigned Capacity=8, StockCapacity=4;
inline constexpr uint64_t Guid=0x5bf177da3487bULL, StockGuid=0x5bf177da34879ULL;
inline constexpr const char* LobbyHello="OEH6";
inline constexpr uint32_t LobbyHelloId=0x3648454fu;
inline constexpr const char* Title="Sega Rally 2";
inline constexpr const char* EightTitle="Sega Rally 2 - 8 Player";
template<class Config> inline bool Selected(const Config& config){return config["NetSrally2Eight"].template ValueAsDefault<bool>(false);}
inline constexpr unsigned CabinetCapacity(bool eight){return eight?Capacity:StockCapacity;}
inline constexpr uint64_t LinkGuid(bool eight){return eight?Guid:StockGuid;}
}
#endif
