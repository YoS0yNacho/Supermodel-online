// Saved discovery servers for the Online launcher. GPL-3.0-or-later.
#ifndef INCLUDED_ONLINE_SERVERLIST_H
#define INCLUDED_ONLINE_SERVERLIST_H
#include "Network/RelayProtocol.h"
#include "ServerDefaults.h"
#include "Util/NewConfig.h"
#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

namespace Online {

class ServerList {
public:
  static constexpr size_t MaxServers = 32;
  ServerList() { Reset(); }
  static bool Normalize(const std::string& raw, std::string& endpoint) {
    const auto first = raw.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return false;
    std::string candidate = raw.substr(first, raw.find_last_not_of(" \t\r\n") - first + 1);
    if (candidate.size() > 255) return false;
    if (candidate.find(':') == std::string::npos) candidate += ":" + std::to_string(DefaultRelayPort);
    std::string host; uint16_t port;
    if (!RelayProtocol::ParseEndpoint(candidate, host, port)) return false;
    // DNS hostnames are case-insensitive. Canonicalize before deduplicating.
    for (char& c : host) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    candidate = host + ":" + std::to_string(unsigned(port));
    if (candidate.size() > 255) return false;
    endpoint = candidate;
    return true;
  }
  const std::vector<std::string>& Entries() const { return m_servers; }
  bool Contains(const std::string& raw) const {
    std::string endpoint;
    return Normalize(raw, endpoint) &&
        std::find(m_servers.begin(), m_servers.end(), endpoint) != m_servers.end();
  }
  static bool IsDefault(const std::string& raw) {
    std::string endpoint;
    if (!Normalize(raw, endpoint)) return false;
    for (const char* item : DefaultRelayEndpoints) if (endpoint == item) return true;
    return false;
  }
  bool Add(const std::string& raw) {
    std::string endpoint;
    if (!Normalize(raw, endpoint) || Contains(endpoint) || m_servers.size() >= MaxServers) return false;
    m_servers.push_back(endpoint);
    return true;
  }
  bool Remove(const std::string& raw) {
    std::string endpoint;
    if (!Normalize(raw, endpoint) || IsDefault(endpoint)) return false;
    auto it = std::find(m_servers.begin(), m_servers.end(), endpoint);
    if (it == m_servers.end()) return false;
    m_servers.erase(it);
    return true;
  }
  static std::string Selected(const Util::Config::Node& config) {
    std::string endpoint;
    return Normalize(config["OnlineRelay"].ValueAsDefault<std::string>(DefaultRelayEndpoint), endpoint)
        ? endpoint : DefaultRelayEndpoint;
  }
  void Load(const Util::Config::Node& config) {
    Reset();
    // Migrate the previous single-server setting without losing a custom relay.
    // Reserve its slot even if the persisted list has reached the size bound.
    const auto selected = Selected(config);
    const auto raw = config["OnlineServers"].ValueAsDefault<std::string>("");
    for (size_t start = 0; start < raw.size() &&
        m_servers.size() < (Contains(selected) ? MaxServers : MaxServers - 1);) {
      const auto end = raw.find(',', start);
      Add(raw.substr(start, end == std::string::npos ? end : end - start));
      if (end == std::string::npos) break;
      start = end + 1;
    }
    Add(selected);
  }
  void Store(Util::Config::Node& config) const {
    std::string joined;
    for (const auto& endpoint : m_servers) {
      if (!joined.empty()) joined += ',';
      joined += endpoint;
    }
    config.Set("OnlineServers", joined);
  }
private:
  std::vector<std::string> m_servers;
  void Reset() { m_servers.assign(std::begin(DefaultRelayEndpoints), std::end(DefaultRelayEndpoints)); }
};
}
#endif
