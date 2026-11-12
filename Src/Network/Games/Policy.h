// Selection only; native policies stay in their game module. GPL-3.0-or-later.
#ifndef INCLUDED_GAME_NETWORK_POLICY_H
#define INCLUDED_GAME_NETWORK_POLICY_H
#include "Daytona2/Policy.h"
#include "Lemans24/Policy.h"
#include "Scud/Policy.h"
#include "Srally2/Policy.h"
namespace GameNetwork {
inline bool IsSupportedGame(const std::string& n) { return Dayto2pe::IsSupportedGame(n)||Scud::IsSupportedGame(n)||Srally2::IsSupportedGame(n)||Lemans24::IsSupportedGame(n); }
inline uint64_t GUID(const std::string& n,bool rallyEight=false) { return Dayto2pe::IsSupportedGame(n)?Dayto2pe::ProtocolGUID(n):Srally2::IsSupportedGame(n)?Srally2::ProtocolGUID(n,rallyEight):Lemans24::IsSupportedGame(n)?Lemans24::ProtocolGUID(n):Scud::ProtocolGUID(n); }
inline unsigned PacketSize(const std::string& n) { return Dayto2pe::IsSupportedGame(n)?0x350:Scud::IsSupportedGame(n)?Scud::PacketSize:Srally2::IsSupportedGame(n)?2048:Lemans24::IsSupportedGame(n)?Lemans24::PacketSize:0; }
inline unsigned MaxCabinets(const std::string& n,bool rallyEight=false) { return Dayto2pe::IsSupportedGame(n)?Dayto2pe::MaxCabinets:Srally2::IsSupportedGame(n)?Srally2Research::CabinetCapacity(rallyEight):Lemans24::IsSupportedGame(n)?Lemans24::MaxCabinets:8u; }
}
#endif
