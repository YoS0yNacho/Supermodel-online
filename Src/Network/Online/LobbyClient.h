// GUI lobby control and room-scoped online chat. No guest memory or game-specific packet code. GPL-3.0-or-later.
#ifndef INCLUDED_ONLINE_LOBBYCLIENT_H
#define INCLUDED_ONLINE_LOBBYCLIENT_H
#include "RaceStandings.h"
#include "RoomSettings.h"
#include "Network/Games/Srally2/ResearchBuild.h"
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace Online {
class PlayerHost;
inline constexpr const char* Games[] = {"daytona2", "dayto2pe", "scud", "scudplus", "srally2", "lemans24", "srally2_8p"};
inline constexpr const char* Titles[] = {"Daytona USA 2", "Daytona USA 2 Power Edition", "Scud Race", "Scud Race Plus", Srally2Research::Title, "Le Mans 24", Srally2Research::EightTitle};
inline constexpr unsigned GameCount = sizeof(Games) / sizeof(Games[0]);
inline constexpr uint32_t GameMaskAll = (1u << GameCount) - 1;
inline constexpr unsigned MaxPlayers(unsigned game) { return game < 2 ? 16u : game == 4 ? Srally2Research::StockCapacity : game == 5 ? 6u : 8u; }
// Both Rally entries use the exact same, unmodified owner archive.
inline constexpr const char* RomGame(unsigned game){return game==6?"srally2":game<GameCount?Games[game]:"";}
inline constexpr size_t MaxChatLength = 159;
inline constexpr uint16_t DefaultRelayPort = 1970;
inline constexpr const char* DefaultRelayHost = "onlinetest.viewdns.net";
inline constexpr const char* DefaultRelayEndpoint = "onlinetest.viewdns.net:1970";
inline constexpr const char* DefaultPlayerName = "Player";

struct Member { uint32_t id = 0; std::string name; uint32_t carNumber = 0; };
struct Room {
  uint32_t id = 0, game = 0, state = 0, master = 0;
  std::string name;
  uint32_t udpMode=0, udpPort=0;bool hostChecking=false;
  uint32_t options=0;bool optionsSupported=false,nativeSettingsSupported=false,nativeSettingsReceived=false;
  std::vector<Member> members;
};
struct ChatMessage {
  uint32_t seq = 0, scope = 0, car = 0;
  std::string sender, text;
};
struct Snapshot {
  bool connecting = false, connected = false, launch = false, hostedRoom = false;
  uint32_t self = 0, room = 0, game = 0, index = 0, count = 0, roomOptions = 0, carNumber = 0;
  std::string status = "Not connected", error, endpoint, ticket, gameEndpoint;
  // Original discovery server, never overwritten by a creator-room redirect.
  std::string directoryEndpoint;
  std::shared_ptr<PlayerHost> playerHost;
  std::vector<Room> rooms;
  std::vector<ChatMessage> loungeChat;
  std::vector<ChatMessage> roomChat;
};
class LobbyClient {
public:
  LobbyClient();
  ~LobbyClient();
  LobbyClient(const LobbyClient&) = delete;
  LobbyClient& operator=(const LobbyClient&) = delete;
  // Worker owns its socket and state: DNS/connect/IO never block the GUI.
  void Connect(const std::string& endpoint, const std::string& name, uint32_t available, bool asyncEnabled);
  void Disconnect();
  Snapshot Get() const;
  void Create(uint32_t game, const std::string& title, bool selfHostUdp=false, uint16_t udpPort=1971);
  void Join(uint32_t room);
  void Leave();
  void Start(uint32_t room);
  void SetRoomOptions(uint32_t room, uint32_t options);
  void SetCarNumber(uint32_t room, uint32_t carNumber);
  void SendChat(const std::string& message);
private:
  struct State;
  std::shared_ptr<State> state;
  void Queue(const char* kind, std::vector<uint8_t> body = {});
  static void Worker(std::shared_ptr<State> state, std::string host, uint16_t port,
                     std::string name, uint32_t available, bool asyncEnabled);
};

struct RoomChatSnapshot {
  bool connecting = false, connected = false, closed = false;
  uint32_t room = 0, car = 0, count = 0;
  std::string player, title, status = "Offline", error;
  std::vector<ChatMessage> messages;
  std::vector<StandingRow> standings;
  uint64_t standingsTimeMs = 0;
};
class RoomChatClient {
public:
  RoomChatClient();
  ~RoomChatClient();
  RoomChatClient(const RoomChatClient&) = delete;
  RoomChatClient& operator=(const RoomChatClient&) = delete;
  void Connect(const std::string& endpoint, const std::string& ticketHex, const std::string& fallbackName, std::shared_ptr<PlayerHost> host=nullptr);
  void Disconnect();
  RoomChatSnapshot Get() const;
  void SetStanding(NativeStanding standing);
  void SendChat(const std::string& message);
private:
  struct State;
  std::shared_ptr<State> state;
  static void Worker(std::shared_ptr<State> state, std::string host, uint16_t port,
                     std::array<uint8_t, 16> ticket, std::string fallbackName);
};
}
#endif
