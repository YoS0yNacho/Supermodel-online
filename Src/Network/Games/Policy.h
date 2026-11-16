// Selection only; native policies stay in their game module. GPL-3.0-or-later.
#ifndef INCLUDED_GAME_NETWORK_POLICY_H
#define INCLUDED_GAME_NETWORK_POLICY_H
#include "Daytona2/Policy.h"
#include "Dirtdvls/Policy.h"
#include "Lemans24/Policy.h"
#include "Scud/Policy.h"
#include "Srally2/Policy.h"
namespace GameNetwork {
inline bool IsSupportedGame(const std::string& n) { return Dayto2pe::IsSupportedGame(n)||Scud::IsSupportedGame(n)||Srally2::IsSupportedGame(n)||Lemans24::IsSupportedGame(n)||Dirtdvls::IsSupportedGame(n); }
// `research` selects the compiled research capacity variant of n (Sega Rally 2 8-player, Scud Race Plus 16-player).
template<class Config> inline bool ResearchSelected(const std::string& n,const Config& c) { return Srally2::IsSupportedGame(n)?Srally2Research::Selected(c):n=="scudplus"?ScudResearch::Selected(c):false; }
inline uint64_t GUID(const std::string& n,bool research=false) { return Dayto2pe::IsSupportedGame(n)?Dayto2pe::ProtocolGUID(n):Srally2::IsSupportedGame(n)?Srally2::ProtocolGUID(n,research):Lemans24::IsSupportedGame(n)?Lemans24::ProtocolGUID(n):Dirtdvls::IsSupportedGame(n)?Dirtdvls::ProtocolGUID(n):Scud::ProtocolGUID(n,research); }
inline unsigned PacketSize(const std::string& n) { return Dayto2pe::IsSupportedGame(n)?0x350:Scud::IsSupportedGame(n)?Scud::PacketSize:Srally2::IsSupportedGame(n)?2048:Lemans24::IsSupportedGame(n)?Lemans24::PacketSize:Dirtdvls::IsSupportedGame(n)?Dirtdvls::PacketSize:0; }
inline unsigned MaxCabinets(const std::string& n,bool research=false) { return Dayto2pe::IsSupportedGame(n)?Dayto2pe::MaxCabinets:Srally2::IsSupportedGame(n)?Srally2Research::CabinetCapacity(research):Lemans24::IsSupportedGame(n)?Lemans24::MaxCabinets:Dirtdvls::IsSupportedGame(n)?Dirtdvls::MaxCabinets:Scud::CabinetCapacity(n,research); }
}
#endif
