// Session-only discovery return routing. GPL-3.0-or-later.
#ifndef INCLUDED_ONLINE_LOBBYRETURN_H
#define INCLUDED_ONLINE_LOBBYRETURN_H
#include <string>
namespace Online {
// Room/chat transport may be loopback for the creator. It is not the lobby
// destination. Never guess a port or use an INI default when the original
// connected discovery server is known. Legacy callers retain their endpoint.
inline std::string ReturnRelay(const std::string& directory, const std::string& room) {
  return directory.empty() ? room : directory;
}
}
#endif
