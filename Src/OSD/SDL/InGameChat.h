// Online in-game chat overlay. GPL-3.0-or-later.
#ifndef INCLUDED_INGAMECHAT_H
#define INCLUDED_INGAMECHAT_H

#include "SDLIncludes.h"
#include "Network/Online/LobbyClient.h"
#include <chrono>
#include <cstdint>
#include <string>

class InGameChat {
public:
  InGameChat() = default;
  ~InGameChat();
  InGameChat(const InGameChat&) = delete;
  InGameChat& operator=(const InGameChat&) = delete;

  bool Init(SDL_Window* window, SDL_GLContext glContext,
            const std::string& endpoint, const std::string& ticket,
            const std::string& playerName, std::shared_ptr<Online::PlayerHost> host=nullptr);
  void Shutdown();

  bool IsActive() const { return m_initialized; }
  bool IsTyping() const { return m_typing; }
  bool ReturnToLobbyRequested() const { return m_returnToLobby; }

  // Drains SDL events when active. Returns false on SDL_QUIT.
  bool ProcessEvents(bool& suppressKeyboard);

  // Renders HUD overlay onto the default backbuffer right before SwapWindow.
  void RenderOverlay();
  // Called between completed frames; memory sampling stays outside the UI.
  void UpdateStanding(Online::NativeStanding standing);

private:
  using Clock = std::chrono::steady_clock;

  Online::RoomChatClient m_client;
  SDL_Window* m_window = nullptr;
  bool m_initialized = false;
  bool m_returnToLobby = false;
  bool m_typing = false;
  bool m_focusInput = false;
  bool m_inputActive = false;
  bool m_latchRelease = false;
  bool m_hudMuted = false;
  bool m_standingVisible = false;
  Online::StandingLifecycle m_standingLifecycle;
  uint32_t m_lastSeenSeq = 0;
  uint32_t m_lastScrolledSeq = 0;
  Clock::time_point m_startTime{};
  Clock::time_point m_lastActivity{};
  Clock::time_point m_statusNoticeUntil{};
  std::string m_statusNotice;
  std::string m_pendingChars;
  char m_input[160] = "";
};

#endif
