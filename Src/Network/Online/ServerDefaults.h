// Ordered default discovery servers. GPL-3.0-or-later.
#ifndef INCLUDED_ONLINE_SERVERDEFAULTS_H
#define INCLUDED_ONLINE_SERVERDEFAULTS_H
#include <cstdint>
namespace Online {
inline constexpr uint16_t DefaultRelayPort = 1970;
inline constexpr const char* DefaultRelayHost = "onlinetest-useast.viewdns.net";
inline constexpr const char* DefaultRelayEndpoint = "onlinetest-useast.viewdns.net:1970";
inline constexpr const char* DefaultRelayEndpoints[] = {
  DefaultRelayEndpoint, "onlinetest.viewdns.net:1970"
};
inline constexpr const char* DefaultRelayServers =
    "onlinetest-useast.viewdns.net:1970,onlinetest.viewdns.net:1970";

}
#endif
