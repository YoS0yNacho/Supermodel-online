// Launcher Online tab + Lobby/Lounge chat. GPL-3.0-or-later.
#ifndef INCLUDED_ONLINEGUI_H
#define INCLUDED_ONLINEGUI_H
#include "Network/Online/LobbyClient.h"
#include "Network/Online/ServerList.h"
struct OnlineGui {
  Online::LobbyClient client;
  Online::ServerList servers;
  char endpoint[256] = "onlinetest-useast.viewdns.net:1970";
  char player[32] = "Player";
  char title[64] = "My lobby";
  char chatInput[160] = "";
  int game = 0;
  int udpHosting = 0, udpPort = 1971;
  bool autoConnectPending = false;
  bool spectate = false;
  bool cliRelayOverride = false;
  bool saveIdentityRequested = false;
  bool refocusChat = false;
  std::string folder, error, returnNotice;
  uint32_t selectedRoom = 0;
  uint32_t optionsRoom = 0;
  uint32_t lastChatScope = ~0u;
  uint32_t lastChatSeq = 0;
  Online::Snapshot launch;
  void Draw(const std::string& romDirectory);
  bool PollLaunch(const std::string& romDirectory);
  void DrawCreateDialog(const std::string& romDirectory, const Online::Snapshot& snapshot, bool requestOpen);
  void DrawRoomOptionsDialog(const Online::Snapshot& snapshot, bool requestOpen);
  static uint32_t Available(const std::string& romDirectory);
};
#endif
