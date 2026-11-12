// Saved server defaults, migration and real INI round trips. GPL-3.0-or-later.
#include "Network/Online/ServerList.h"
#include "Util/ConfigBuilders.h"
#include <cassert>
#include <iostream>

int main(int argc, char** argv) {
  assert(argc == 2);
  using Online::ServerList;
  Util::Config::Node config("Global");
  ServerList servers;
  servers.Load(config);
  assert(servers.Entries().size() == 2);
  assert(servers.Entries()[0] == "onlinetest-useast.viewdns.net:1970");
  assert(servers.Entries()[1] == "onlinetest.viewdns.net:1970");
  assert(ServerList::Selected(config) == servers.Entries()[0]);
  assert(!servers.Remove(servers.Entries()[0]));
  assert(!servers.Remove(servers.Entries()[1]));
  std::string endpoint;
  assert(ServerList::Normalize("  EXAMPLE.COM \t", endpoint) && endpoint == "example.com:1970");
  assert(ServerList::Normalize("localhost:01970", endpoint) && endpoint == "localhost:1970");
  assert(ServerList::Normalize("127.0.0.1:65535", endpoint));
  for (const std::string bad : {"", " \t", "example.com:0", "example.com:65536", "example.com:-1",
       "example.com:1970;injection", "example.com:1970,other:123", "example.com:1970\nother",
       "http://example.com:1970", "example.com:", "[::1]:1970", "bad host:1970"})
    assert(!ServerList::Normalize(bad, endpoint));
  assert(!ServerList::Normalize(std::string(256, 'a'), endpoint));
  assert(servers.Add("MY-RELAY.EXAMPLE"));
  assert(!servers.Add("my-relay.example:01970"));
  assert(!servers.Add(" ONLINETEST.VIEWDNS.NET "));
  assert(servers.Contains("MY-RELAY.EXAMPLE:1970"));

  // Migrate a legacy single-server INI without replacing the user's selection.
  config.Set("OnlineRelay", std::string(" Custom.Example:2345 "));
  servers.Load(config);
  assert(servers.Entries().size() == 3);
  assert(servers.Entries()[2] == "custom.example:2345");
  assert(ServerList::Selected(config) == "custom.example:2345");
  config.Set("OnlineRelay", std::string("onlinetest.viewdns.net:1970"));
  servers.Load(config);
  assert(servers.Entries().size() == 2);
  assert(ServerList::Selected(config) == servers.Entries()[1]);

  config.Set("OnlineServers", std::string("extra.example,EXTRA.EXAMPLE:1970,bad:0,onlinetest.viewdns.net:1970,other.example:1971"));
  servers.Load(config);
  assert(servers.Entries().size() == 4);
  assert(servers.Entries()[2] == "extra.example:1970");
  assert(servers.Entries()[3] == "other.example:1971");
  assert(servers.Remove("extra.example"));
  assert(!servers.Remove("missing.example"));
  servers.Add("192.0.2.5:4321");
  config.Set("OnlineRelay", std::string("192.0.2.5:4321"));
  config.Set("OnlinePlayer", std::string("My player"));
  config.Set("UnrelatedGlobalSetting", 42);
  config.Set("scud/UnrelatedGameSetting", 123);
  servers.Store(config);
  Util::Config::WriteINIFile(argv[1], config, "Online server list test");
  Util::Config::Node loaded("Global");
  assert(!Util::Config::FromINIFile(&loaded, argv[1]));
  ServerList restored;
  restored.Load(loaded);
  assert(restored.Entries() == servers.Entries());
  assert(ServerList::Selected(loaded) == "192.0.2.5:4321");
  assert(loaded["OnlinePlayer"].ValueAs<std::string>() == "My player");
  assert(loaded["UnrelatedGlobalSetting"].ValueAs<int>() == 42);
  assert(loaded["scud/UnrelatedGameSetting"].ValueAs<int>() == 123);
  restored.Load(loaded);
  assert(restored.Entries() == servers.Entries());

  // Corrupted selection and list entries must not hide or reorder defaults.
  loaded.Set("OnlineRelay", std::string("http://bad/"));
  loaded.Set("OnlineServers", std::string(",bad:0,ONLINETEST.VIEWDNS.NET:1970,,onlinetest-useast.viewdns.net:1970,"));
  restored.Load(loaded);
  assert(restored.Entries().size() == 2);
  assert(ServerList::Selected(loaded) == Online::DefaultRelayEndpoint);

  // A full list stays bounded and never loses a legacy selected custom server.
  std::string many;
  for (unsigned i = 0; i < 60; ++i) many += "server" + std::to_string(i) + ".example:1970,";
  loaded.Set("OnlineServers", many);
  loaded.Set("OnlineRelay", std::string("keep-this.example:1970"));
  restored.Load(loaded);
  assert(restored.Entries().size() == ServerList::MaxServers);
  assert(restored.Contains("keep-this.example"));
  assert(!restored.Add("one-too-many.example"));
  restored.Store(loaded);
  ServerList fullRoundTrip;
  fullRoundTrip.Load(loaded);
  assert(fullRoundTrip.Entries() == restored.Entries());
  std::cout << "PASS default server order, validation, deduplication, legacy migration, removal, real INI round trip, preserved settings and bounded lists\n";
}
