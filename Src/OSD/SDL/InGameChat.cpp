// Online in-game chat overlay. GPL-3.0-or-later.
#include <GL/glew.h>
#include "InGameChat.h"
#include "Pkgs/imgui/imgui.h"
#include "Pkgs/imgui/imgui_impl_sdl2.h"
#include "Pkgs/imgui/imgui_impl_opengl3.h"
#include <algorithm>
#include <cstring>

namespace {
bool HasPrintableText(const char* text) {
  if (!text) return false;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p) {
    if (*p > 32 && *p != 127) return true;
  }
  return false;
}

ImVec4 CarBadgeColor(uint32_t car, float alpha) {
  switch (car) {
    case 1: return ImVec4(1.00f, 0.82f, 0.28f, alpha);
    case 2: return ImVec4(0.38f, 0.82f, 1.00f, alpha);
    case 3: return ImVec4(0.45f, 0.94f, 0.54f, alpha);
    case 4: return ImVec4(1.00f, 0.52f, 0.72f, alpha);
    case 5: return ImVec4(1.00f, 0.64f, 0.30f, alpha);
    case 6: return ImVec4(0.76f, 0.58f, 1.00f, alpha);
    case 7: return ImVec4(0.78f, 0.96f, 0.38f, alpha);
    case 8: return ImVec4(0.36f, 0.94f, 0.86f, alpha);
    default: return ImVec4(0.68f, 0.84f, 1.00f, alpha);
  }
}

const char* SelectGlslVersion() {
  GLint profile = 0;
  GLint major = 0;
  glGetIntegerv(GL_MAJOR_VERSION, &major);
#ifdef GL_CONTEXT_PROFILE_MASK
  if (major >= 3) glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile);
  if (profile & GL_CONTEXT_CORE_PROFILE_BIT) return "#version 410";
#endif
  if (major >= 3) return "#version 130";
  return "#version 120";
}
}

InGameChat::~InGameChat() {
  Shutdown();
}

bool InGameChat::Init(SDL_Window* window, SDL_GLContext glContext,
                      const std::string& endpoint, const std::string& ticket,
                      const std::string& playerName, std::shared_ptr<Online::PlayerHost> host) {
  Shutdown();
  if (!window || !glContext || endpoint.empty() || ticket.empty())
    return false;

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.LogFilename = nullptr;
  // Never enable ImGui gamepad navigation in-game: wheels/controllers drive the car.
  io.ConfigFlags &= ~(ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NavEnableKeyboard);
  io.Fonts->AddFontDefaultVector();

  ImGui::StyleColorsDark();
  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowRounding = 6.0f;
  style.ChildRounding = 4.0f;
  style.FrameRounding = 4.0f;
  style.WindowBorderSize = 1.0f;

  if (!ImGui_ImplSDL2_InitForOpenGL(window, glContext)) {
    ImGui::DestroyContext();
    return false;
  }
  if (!ImGui_ImplOpenGL3_Init(SelectGlslVersion())) {
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    return false;
  }

  SDL_StopTextInput();
  m_window = window;
  m_initialized = true;
  m_returnToLobby = false;
  m_typing = false;
  m_focusInput = false;
  m_inputActive = false;
  m_latchRelease = false;
  m_hudMuted = false;
  m_standingVisible = false;
  m_standingLifecycle.Reset();
  m_lastSeenSeq = 0;
  m_lastScrolledSeq = 0;
  m_pendingChars.clear();
  m_input[0] = '\0';
  m_startTime = Clock::now();
  m_lastActivity = m_startTime;
  m_statusNotice = "Online Chat: Enter = type, Tab = peek, F8 = mute HUD";
  m_statusNoticeUntil = m_startTime + std::chrono::seconds(6);
  m_client.Connect(endpoint, ticket, playerName, std::move(host));
  return true;
}

void InGameChat::Shutdown() {
  m_client.Disconnect();
  if (m_initialized) {
    if (m_typing) SDL_StopTextInput();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    m_initialized = false;
  }
  m_standingVisible = false;
  m_standingLifecycle.Reset();
  m_window = nullptr;
  m_typing = false;
  m_focusInput = false;
  m_inputActive = false;
  m_latchRelease = false;
  m_pendingChars.clear();
}

bool InGameChat::ProcessEvents(bool& suppressKeyboard) {
  if (!m_initialized) {
    suppressKeyboard = false;
    return true;
  }

  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    if (e.type == SDL_QUIT) {
      suppressKeyboard = false;
      return false;
    }

    if (e.type == SDL_KEYDOWN) {
      const SDL_Keycode sym = e.key.keysym.sym;
      const Uint16 mod = e.key.keysym.mod;
      const bool altHeld = (mod & KMOD_ALT) != 0;
      const bool isEnter = (sym == SDLK_RETURN || sym == SDLK_KP_ENTER);

      if (!m_typing) {
        if (isEnter && !altHeld && e.key.repeat == 0) {
          m_typing = true;
          m_focusInput = true;
          m_inputActive = false;
          m_pendingChars.clear();
          m_lastActivity = Clock::now();
          SDL_StartTextInput();
          continue; // swallow opening Enter so ImGui does not immediately submit
        }
        if (sym == SDLK_TAB && !altHeld && e.key.repeat == 0) {
          m_lastActivity = Clock::now();
          continue;
        }
        if (sym == SDLK_F8 && !altHeld && e.key.repeat == 0) {
          m_hudMuted = !m_hudMuted;
          m_statusNotice = m_hudMuted ? "Chat HUD muted (press F8 to show, Enter to chat)"
                                      : "Chat HUD enabled";
          m_statusNoticeUntil = Clock::now() + std::chrono::seconds(3);
          m_lastActivity = Clock::now();
          continue;
        }
      } else {
        if (sym == SDLK_ESCAPE) {
          m_typing = false;
          m_focusInput = false;
          m_inputActive = false;
          m_pendingChars.clear();
          m_input[0] = '\0';
          m_latchRelease = true;
          SDL_StopTextInput();
          continue;
        }
        if (isEnter && !altHeld) {
          if (!m_pendingChars.empty()) {
            const size_t cur = std::strlen(m_input);
            if (cur + 1 < sizeof(m_input)) {
              const size_t avail = sizeof(m_input) - 1 - cur;
              const size_t n = (std::min)(avail, m_pendingChars.size());
              std::memcpy(m_input + cur, m_pendingChars.data(), n);
              m_input[cur + n] = '\0';
            }
            m_pendingChars.clear();
          }
          if (HasPrintableText(m_input)) {
            m_client.SendChat(m_input);
            m_lastActivity = Clock::now();
          }
          m_input[0] = '\0';
          m_typing = false;
          m_focusInput = false;
          m_inputActive = false;
          m_latchRelease = true;
          SDL_StopTextInput();
          continue;
        }
      }
    }

    if (m_typing && !m_inputActive && e.type == SDL_TEXTINPUT) {
      if (m_pendingChars.size() < sizeof(m_input) - 1)
        m_pendingChars += e.text.text;
      continue;
    }

    ImGui_ImplSDL2_ProcessEvent(&e);
  }

  const Uint8* ks = SDL_GetKeyboardState(nullptr);
  const bool closingKeyDown = ks && (ks[SDL_SCANCODE_ESCAPE] ||
                                     ks[SDL_SCANCODE_RETURN] ||
                                     ks[SDL_SCANCODE_KP_ENTER]);
  if (m_latchRelease && !closingKeyDown) {
    m_latchRelease = false;
  }
  suppressKeyboard = m_typing || m_latchRelease;
  return true;
}

void InGameChat::UpdateStanding(Online::NativeStanding standing) {
  if (!m_initialized) return;
  const auto current = m_standingLifecycle.Update(standing);
  m_standingVisible = current.Visible();
  m_client.SetStanding(current);
}

void InGameChat::RenderOverlay() {
  if (!m_initialized || !m_window)
    return;

  const auto now = Clock::now();
  const auto snap = m_client.Get();
  if(snap.closed)m_returnToLobby=true;

  const uint32_t newestSeq = snap.messages.empty() ? 0u : snap.messages.back().seq;
  if (newestSeq > m_lastSeenSeq) {
    m_lastSeenSeq = newestSeq;
    m_lastActivity = now;
  }

  constexpr double kHoldSeconds = 8.0;
  constexpr double kFadeSeconds = 2.0;
  const double sinceActivity = std::chrono::duration<double>(now - m_lastActivity).count();
  const bool hasNotice = now < m_statusNoticeUntil;

  float hudAlpha = 0.0f;
  if (m_typing) {
    hudAlpha = 1.0f;
  } else if (hasNotice) {
    hudAlpha = 0.92f;
  } else if (!m_hudMuted && !snap.messages.empty() && sinceActivity < kHoldSeconds + kFadeSeconds) {
    hudAlpha = sinceActivity <= kHoldSeconds
      ? 0.92f
      : float(0.92 * (1.0 - (sinceActivity - kHoldSeconds) / kFadeSeconds));
  }

  const uint64_t nowMs = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
  const bool showStandings = m_standingVisible && snap.connected && !snap.closed &&
    snap.standingsTimeMs && nowMs - snap.standingsTimeMs <= 2500 && !snap.standings.empty();
  if (hudAlpha <= 0.005f && !showStandings)
    return;

  GLint prevDrawFbo = 0;
  GLboolean prevColorMask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
  GLboolean prevDepthMask = GL_TRUE;
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFbo);
  glGetBooleanv(GL_COLOR_WRITEMASK, prevColorMask);
  glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);

  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplSDL2_NewFrame();

  ImGuiIO& io = ImGui::GetIO();
  if (m_typing && m_inputActive && !m_pendingChars.empty()) {
    io.AddInputCharactersUTF8(m_pendingChars.c_str());
    m_pendingChars.clear();
  }
  const float dispW = (std::max)(320.0f, io.DisplaySize.x);
  const float dispH = (std::max)(240.0f, io.DisplaySize.y);
  const float fontScale = std::clamp((std::min)(dispW / 900.0f, dispH / 560.0f), 0.82f, 1.65f);
  ImGui::GetStyle().FontScaleMain = fontScale;

  ImGui::NewFrame();

  if (showStandings) {
    const float inset = 10.0f * fontScale;
    ImGui::SetNextWindowPos(ImVec2(inset, inset), ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(150.0f * fontScale, 0), ImVec2(dispW * 0.48f, dispH - 2 * inset));
    ImGui::SetNextWindowBgAlpha(0.66f);
    const ImGuiWindowFlags boardFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize |
      ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing;
    if (ImGui::Begin("##OnlineRaceStandings", nullptr, boardFlags)) {
      // Scale the standings board to keep all rows visible.
      float widest = 0.0f;
      for (const auto& row : snap.standings) {
        const std::string label = std::to_string(row.car) + "P: " + row.name + (row.rank ? "" : " ...");
        widest = (std::max)(widest, ImGui::CalcTextSize(label.c_str()).x);
      }
      const float heightScale = (dispH - 60.0f * fontScale) /
        ((snap.standings.size() + 2) * ImGui::GetTextLineHeightWithSpacing());
      const float widthScale = (dispW * 0.48f - 2 * ImGui::GetStyle().WindowPadding.x) / (std::max)(1.0f, widest);
      const float boardScale = (std::min)(1.0f, (std::min)(heightScale, widthScale));
      ImGui::SetWindowFontScale(boardScale);
      ImGui::TextDisabled("ONLINE POSITIONS");
      ImGui::Separator();
      for (const auto& row : snap.standings) {
        ImGui::TextColored(CarBadgeColor(row.car, row.car == snap.car ? 1.0f : 0.85f),
          "%uP: %s", unsigned(row.car), row.name.c_str());
        if (!row.rank) { ImGui::SameLine(); ImGui::TextDisabled("..."); }
      }
    }
    ImGui::End();
  }

  const float pad = 12.0f * fontScale;
  const float boxW = std::clamp(dispW * 0.44f, 240.0f * fontScale, 480.0f * fontScale);
  ImGui::SetNextWindowPos(ImVec2(pad, dispH - pad), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
  ImGui::SetNextWindowSizeConstraints(ImVec2(boxW, 0.0f), ImVec2(boxW, dispH * 0.52f));
  ImGui::SetNextWindowBgAlpha(hudAlpha * (m_typing ? 0.78f : 0.56f));

  ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                           ImGuiWindowFlags_NoResize |
                           ImGuiWindowFlags_NoMove |
                           ImGuiWindowFlags_NoSavedSettings |
                           ImGuiWindowFlags_AlwaysAutoResize;
  if (!m_typing) {
    flags |= ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing;
  } else {
    flags |= ImGuiWindowFlags_NoNavInputs;
  }

  ImGui::PushStyleVar(ImGuiStyleVar_Alpha, hudAlpha);
  if (ImGui::Begin("##OnlineInGameChat", nullptr, flags)) {
    if (m_typing) {
      if (snap.connected && snap.car > 0) {
        ImGui::TextColored(CarBadgeColor(snap.car, 1.0f), "[Car %u] %s", unsigned(snap.car), snap.player.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("| %s", snap.title.empty() ? "Online Chat" : snap.title.c_str());
      } else {
        ImGui::TextDisabled("Online Chat (%s)", snap.status.c_str());
      }
      ImGui::Separator();
      if (snap.connected && snap.car == 1) {
        if (ImGui::Button("Return everyone to lobby"))
          ImGui::OpenPopup("End online session?");
        if (ImGui::BeginPopupModal("End online session?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
          ImGui::TextUnformatted("End this link and return all players to Online lobbies?");
          if (ImGui::Button("Return to lobby")) {
            m_returnToLobby = true;
            ImGui::CloseCurrentPopup();
          }
          ImGui::SameLine();
          if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
          ImGui::EndPopup();
        }
        ImGui::Separator();
      }

      const float histHeight = std::clamp(dispH * 0.24f, 80.0f * fontScale, 190.0f * fontScale);
      if (ImGui::BeginChild("InGameChatScroll", ImVec2(boxW - 16.0f * fontScale, histHeight), false)) {
        if (snap.messages.empty()) {
          ImGui::TextDisabled("No messages yet. Say hi to the room!");
        } else {
          ImGui::PushTextWrapPos(0.0f);
          for (const auto& m : snap.messages) {
            if (m.car > 0) {
              ImGui::TextColored(CarBadgeColor(m.car, 1.0f), "[Car %u] %s:", unsigned(m.car), m.sender.c_str());
            } else {
              ImGui::TextColored(CarBadgeColor(0, 1.0f), "%s:", m.sender.c_str());
            }
            ImGui::SameLine(0.0f, 5.0f * fontScale);
            ImGui::TextUnformatted(m.text.c_str());
          }
          ImGui::PopTextWrapPos();
        }
        if (newestSeq != m_lastScrolledSeq || m_focusInput) {
          ImGui::SetScrollHereY(1.0f);
          m_lastScrolledSeq = newestSeq;
        }
      }
      ImGui::EndChild();

      const float sendW = 54.0f * fontScale;
      ImGui::SetNextItemWidth((std::max)(80.0f, boxW - 16.0f * fontScale - sendW - ImGui::GetStyle().ItemSpacing.x));
      if (m_focusInput || (!m_inputActive && !ImGui::IsAnyItemActive())) {
        ImGui::SetKeyboardFocusHere(0);
        m_focusInput = false;
      }
      ImGui::InputTextWithHint("##InGameChatInput", "Type message... (Enter=send, Esc=cancel)", m_input, sizeof(m_input));
      m_inputActive = ImGui::IsItemActive();
      ImGui::SameLine();
      const bool canSend = HasPrintableText(m_input);
      ImGui::BeginDisabled(!canSend);
      if (ImGui::Button("Send", ImVec2(sendW, 0.0f)) && canSend) {
        m_client.SendChat(m_input);
        m_input[0] = '\0';
        m_pendingChars.clear();
        m_typing = false;
        m_inputActive = false;
        m_latchRelease = true;
        m_lastActivity = now;
        SDL_StopTextInput();
      }
      ImGui::EndDisabled();
    } else {
      if (!m_hudMuted && !snap.messages.empty() && sinceActivity < kHoldSeconds + kFadeSeconds) {
        const size_t total = snap.messages.size();
        const size_t start = total > 6 ? total - 6 : 0;
        ImGui::PushTextWrapPos(boxW - 12.0f * fontScale);
        for (size_t i = start; i < total; ++i) {
          const auto& m = snap.messages[i];
          if (m.car > 0) {
            ImGui::TextColored(CarBadgeColor(m.car, hudAlpha), "[Car %u] %s:", unsigned(m.car), m.sender.c_str());
          } else {
            ImGui::TextColored(CarBadgeColor(0, hudAlpha), "%s:", m.sender.c_str());
          }
          ImGui::SameLine(0.0f, 5.0f * fontScale);
          ImGui::TextUnformatted(m.text.c_str());
        }
        ImGui::PopTextWrapPos();
      }
      if (hasNotice) {
        ImGui::TextDisabled("%s", m_statusNotice.c_str());
      }
    }
  }
  ImGui::End();
  ImGui::PopStyleVar();

  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

  glColorMask(prevColorMask[0], prevColorMask[1], prevColorMask[2], prevColorMask[3]);
  glDepthMask(prevDepthMask);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GLuint(prevDrawFbo));
}
