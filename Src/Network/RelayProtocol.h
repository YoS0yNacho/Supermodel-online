// Outbound-only relay registration wire format. GPL-3.0-or-later.
#ifndef INCLUDED_RELAYPROTOCOL_H
#define INCLUDED_RELAYPROTOCOL_H
#include "NetProtocol.h"
#include <string>
namespace RelayProtocol {
constexpr uint32_t Version = 1;
constexpr uint32_t MinTickRate = 5;
constexpr uint32_t MaxTickRate = 60;
constexpr uint32_t DefaultTickRate = 60;
#pragma pack(push,1)
struct Join : NetProtocol::MessageHeader {
  static constexpr uint32_t ID = NetProtocol::ID("RLJ1");
  uint32_t version = Version, channel = 0;
  uint64_t nonce[2] = {}, guid = 0;
  Join() : MessageHeader(ID,sizeof(Join)) {}
};
struct LobbyJoin : Join {
  static constexpr uint32_t ID = NetProtocol::ID("RLJ2");
  uint8_t ticket[16] = {};
  LobbyJoin() { id=ID; size=sizeof(LobbyJoin); }
};
struct Ready : NetProtocol::MessageHeader {
  static constexpr uint32_t ID = NetProtocol::ID("RLA1");
  uint32_t version = Version, index = 0, count = 0, channel = 0;
  Ready() : MessageHeader(ID,sizeof(Ready)) {}
};
struct TickRate : NetProtocol::MessageHeader {
  static constexpr uint32_t ID = NetProtocol::ID("RLT1");
  uint32_t version = Version, tickRate = DefaultTickRate;
  TickRate() : MessageHeader(ID,sizeof(TickRate)) {}
};
// RLT2: the host adapts the tick rate during the session between `floor` and
// `cap` (RLT1 before RLA1 carries the starting rate) and will send further RLT1
// messages while the room runs. Sent only to clients whose lobby HELLO set the
// adaptive capability bit; such clients report queueing delay / loss in their
// UDP hellos so the host can see downlink congestion too.
struct AdaptiveTick : NetProtocol::MessageHeader {
  static constexpr uint32_t ID = NetProtocol::ID("RLT2");
  uint32_t version = Version, flags = 1, cap = DefaultTickRate, floor = 10;
  AdaptiveTick() : MessageHeader(ID,sizeof(AdaptiveTick)) {}
};
// RLS1: the relay fans every timed ring segment out to all cabinets itself
// (hop count rewritten per receiver, origin echoed at once); cabinets must not
// forward peer segments. Sent before RLA1, only when every member's lobby
// client advertised the capability.
struct Star : NetProtocol::MessageHeader {
  static constexpr uint32_t ID = NetProtocol::ID("RLS1");
  uint32_t version = Version, flags = 0;
  Star() : MessageHeader(ID,sizeof(Star)) {}
};
struct Error : NetProtocol::MessageHeader {
  static constexpr uint32_t ID = NetProtocol::ID("RLE1");
  uint32_t code = 0, reserved = 0;
  Error() : MessageHeader(ID,sizeof(Error)) {}
};
struct HybridJoin : LobbyJoin {
  static constexpr uint32_t ID = NetProtocol::ID("RLJ3");
  HybridJoin() { id=ID; }
};
struct UdpConfig : NetProtocol::MessageHeader {
  static constexpr uint32_t ID = NetProtocol::ID("RLU1");
  uint32_t version=1,port=0,index=0,count=0;
  uint8_t token[16]={};
  UdpConfig() : MessageHeader(ID,sizeof(UdpConfig)) {}
};
// RLU2 extends UdpConfig with host IPv4 bytes, port, tick rate, roster count,
// and (only for cabinet zero) 4-byte IPv4 + 16-byte token per peer.
static constexpr uint32_t UdpHostConfigID=NetProtocol::ID("RLU2");
#pragma pack(pop)
static_assert(sizeof(HybridJoin)==56 && sizeof(UdpConfig)==40,"Hybrid wire sizes");
static_assert(sizeof(Join)==40 && sizeof(Ready)==24 && sizeof(TickRate)==16 && sizeof(Error)==16 && sizeof(Star)==16 && sizeof(AdaptiveTick)==24,"Relay wire sizes");
static_assert(sizeof(LobbyJoin)==56,"Lobby ticket wire size");
inline bool ParseTicket(const std::string& text, uint8_t (&bytes)[16]) {
  if(text.size()!=32) return false;
  bool nonzero=false;
  for(size_t i=0;i<16;++i) {
    unsigned value=0;
    for(size_t j=0;j<2;++j) {
      char c=text[2*i+j]; unsigned n;
      if(c>='0'&&c<='9') n=c-'0'; else if(c>='a'&&c<='f') n=c-'a'+10; else return false;
      value=(value<<4)|n;
    }
    bytes[i]=uint8_t(value);nonzero|=value!=0;
  }
  return nonzero;
}
// SDL_net 2 clients support IPv4 addresses/hostnames, not IPv6 literals.
inline bool ParseEndpoint(const std::string& text, std::string& host, uint16_t& port) {
  const auto pos=text.find(':');
  if (pos==std::string::npos || pos==0 || pos>253 || pos+1==text.size() || text.find(':',pos+1)!=std::string::npos) return false;
  for (size_t i=0;i<pos;++i) {
    const unsigned char c=text[i];
    if (!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-')) return false;
  }
  unsigned value=0;
  for (size_t i=pos+1;i<text.size();++i) {
    if (text[i]<'0'||text[i]>'9') return false;
    value=value*10+unsigned(text[i]-'0'); if(value>65535) return false;
  }
  if (!value) return false;
  host=text.substr(0,pos);port=uint16_t(value);return true;
}
}
#endif
