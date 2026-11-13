// Online launcher UI. GPL-3.0-or-later.
#include "OnlineGui.h"
#include "Network/RelayProtocol.h"
#include "Pkgs/imgui/imgui.h"
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <filesystem>

namespace {
bool ParseEndpointField(const char* text, std::string& normalized, std::string& host, uint16_t& port) {
  return text && Online::ServerList::Normalize(text, normalized) &&
      RelayProtocol::ParseEndpoint(normalized, host, port);
}
bool HasPrintableText(const char* text) {
  if (!text) return false;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p) {
    if (*p > 32 && *p != 127) return true;
  }
  return false;
}
// Lobby ping colour: green under 60 ms, yellow under 120 ms, red beyond.
ImVec4 PingColor(uint32_t ms) { return ms<60?ImVec4(0.45f,0.9f,0.45f,1):ms<120?ImVec4(0.95f,0.85f,0.3f,1):ImVec4(0.95f,0.4f,0.4f,1); }

ImVec4 CarBadgeColor(uint32_t car) {
  switch (car) {
    case 1: return ImVec4(1.00f, 0.82f, 0.28f, 1.0f);
    case 2: return ImVec4(0.38f, 0.82f, 1.00f, 1.0f);
    case 3: return ImVec4(0.45f, 0.94f, 0.54f, 1.0f);
    case 4: return ImVec4(1.00f, 0.52f, 0.72f, 1.0f);
    case 5: return ImVec4(1.00f, 0.64f, 0.30f, 1.0f);
    case 6: return ImVec4(0.76f, 0.58f, 1.00f, 1.0f);
    case 7: return ImVec4(0.78f, 0.96f, 0.38f, 1.0f);
    case 8: return ImVec4(0.36f, 0.94f, 0.86f, 1.0f);
    default: return ImVec4(0.68f, 0.84f, 1.00f, 1.0f);
  }
}
}

uint32_t OnlineGui::Available(const std::string& dir) {
  uint32_t mask=0;std::error_code ec;
  for(unsigned i=0;i<Online::GameCount;++i) if(std::filesystem::is_regular_file(std::filesystem::path(dir)/(std::string(Online::RomGame(i))+".zip"),ec)) mask|=1u<<i;
  if(!(mask&4)) mask&=~8u; // scudplus needs the scud parent archive
  return mask;
}
bool OnlineGui::PollLaunch(const std::string& dir) {
  if(!folder.empty()&&folder!=dir) {autoConnectPending=false;client.Disconnect();error="ROM folder changed. Reconnect to refresh available games.";}
  folder=dir;
  // Try once; Online always uses AsyncNet and never falls back to sync.
  if(autoConnectPending) {
    std::string normalized, host; uint16_t port;
    autoConnectPending=false;
    if(ParseEndpointField(endpoint,normalized,host,port) && HasPrintableText(player)) {error.clear();client.Connect(normalized,player,Available(dir),true);}
    else error="Enter the relay as host or host:port, for example onlinetest-useast.viewdns.net:1970.";
  }
  const auto s=client.Get();
  if(!s.launch) return false;
  if(s.game>=Online::GameCount||!(Available(dir)&(1u<<s.game))) {
    client.Disconnect();error="Cannot launch: required ROM removed. Recreate the lobby.";return false;
  }
  launch=s;return true;
}
void OnlineGui::Draw(const std::string& dir) {
  const auto s=client.Get();const bool busy=s.connected||s.connecting;
  const float scale=ImGui::GetFontSize()/13.0f;
  std::string normalized, host; uint16_t port;
  ImGui::TextUnformatted("Online lobbies");
  ImGui::BeginDisabled(busy);
  ImGui::SetNextItemWidth(360*scale);
  if (ImGui::BeginCombo("Saved servers", endpoint)) {
    for (const auto& saved : servers.Entries()) {
      const bool selected = saved == endpoint;
      if (ImGui::Selectable(saved.c_str(), selected)) {
        std::snprintf(endpoint, sizeof(endpoint), "%s", saved.c_str());
        cliRelayOverride = false;
        saveIdentityRequested = true;
        error.clear();
      }
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  std::string saveEndpoint;
  const bool canSave = Online::ServerList::Normalize(endpoint, saveEndpoint);
  ImGui::BeginDisabled(!canSave);
  if (ImGui::Button("Save server")) {
    if (servers.Contains(saveEndpoint) || servers.Add(saveEndpoint)) {
      std::snprintf(endpoint, sizeof(endpoint), "%s", saveEndpoint.c_str());
      cliRelayOverride = false;
      saveIdentityRequested = true;
      error.clear();
    } else error = "Saved server list is full (32). Remove a custom server first.";
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!servers.Contains(endpoint) || Online::ServerList::IsDefault(endpoint));
  if (ImGui::Button("Remove server") && servers.Remove(endpoint)) {
    std::snprintf(endpoint, sizeof(endpoint), "%s", Online::DefaultRelayEndpoint);
    cliRelayOverride = false;
    saveIdentityRequested = true;
    error.clear();
  }
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip("The two default servers stay first. Custom servers can be removed.");
  ImGui::SetNextItemWidth(240*scale);
  if (ImGui::InputText("Relay IP:port",endpoint,sizeof(endpoint))) cliRelayOverride=false;
  ImGui::SameLine(0.0f, 16.0f*scale);
  ImGui::SetNextItemWidth(150*scale);ImGui::InputText("Player name",player,sizeof(player));
  ImGui::EndDisabled();
  // Revalidate after widgets so Connect uses the endpoint edited this frame.
  const bool endpointValid=ParseEndpointField(endpoint,normalized,host,port);
  const bool playerValid=HasPrintableText(player);
  ImGui::SameLine(0.0f, 16.0f*scale);
  if(!busy) {
    ImGui::BeginDisabled(!endpointValid||!playerValid);
    if(ImGui::Button("Connect")) {
      autoConnectPending=false;error.clear();folder=dir;
      lastChatScope=~0u;lastChatSeq=0;
      saveIdentityRequested=true;
      if (!cliRelayOverride) servers.Add(normalized);
      std::snprintf(endpoint, sizeof(endpoint), "%s", normalized.c_str());
      client.Connect(normalized,player,Available(dir),true);
    }
    ImGui::EndDisabled();
  } else if(ImGui::Button(s.connecting?"Cancel connection":"Disconnect")) {
    autoConnectPending=false;client.Disconnect();error.clear();selectedRoom=0;
    chatInput[0]='\0';lastChatScope=~0u;lastChatSeq=0;
  }
  ImGui::SameLine();ImGui::TextUnformatted(s.status.c_str());
  if(!returnNotice.empty()) ImGui::TextWrapped("%s",returnNotice.c_str());
  if(!error.empty()) ImGui::TextWrapped("%s",error.c_str());
  if(!s.error.empty()) ImGui::TextWrapped("%s",s.error.c_str());
  if(!s.connected) { DrawCreateDialog(dir,s,false); DrawRoomOptionsDialog(s,false); return; }
  bool openCreateDialog=false, openRoomOptions=false;
  ImGui::Separator();

  const Online::Room* current=nullptr;
  for(const auto& r:s.rooms) if(r.id==s.room) current=&r;

  if(ImGui::BeginTable("OnlineSplitLayout",2,ImGuiTableFlags_BordersInnerV|ImGuiTableFlags_Resizable|ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("LobbyPane",ImGuiTableColumnFlags_WidthStretch,0.56f);
    ImGui::TableSetupColumn("ChatPane",ImGuiTableColumnFlags_WidthStretch,0.44f);
    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    if(current) {
      ImGui::Text("%s  |  %s  |  %u/%u players",current->name.c_str(),Online::Titles[current->game],unsigned(current->members.size()), Online::MaxPlayers(current->game));
      if(s.pingMs){ImGui::SameLine();ImGui::TextUnformatted("|");ImGui::SameLine();ImGui::TextColored(PingColor(s.pingMs),"your ping %u ms",unsigned(s.pingMs));}
      ImGui::TextWrapped("Hosting: %s",current->hostChecking?"Testing TCP/UDP host reachability...":current->udpMode==3?"Creator-hosted room/chat + TCP/UDP":current->udpMode==2?"Creator-hosted TCP + UDP":current->udpMode==1?"Legacy UDP-only host":"Central relay");
      for(size_t i=0;i<current->members.size();++i) {
        const auto& m=current->members[i];
        ImGui::TextColored(CarBadgeColor(uint32_t(i+1)),"[Link %u]",unsigned(i+1));
        if(NativeRoomSettings::CarLimit(current->game) && current->nativeSettingsSupported){ImGui::SameLine();if(m.carNumber)ImGui::Text("CAR %u",unsigned(m.carNumber));else ImGui::TextDisabled("CAR not selected");}
        ImGui::SameLine();
        ImGui::Text("%s  -  %s%s",m.name.c_str(),i==0?"MASTER":"SLAVE",m.id==s.self?" (you)":"");
        ImGui::SameLine();
        if(m.pingMs) ImGui::TextColored(PingColor(m.pingMs),"%u ms",unsigned(m.pingMs));
        else ImGui::TextDisabled("ping ...");
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Round trip from this player to the room host (%s), measured by their lobby poll",current->udpMode>=2?"creator":"relay");
      }
      // Daytona 2 slaves may choose the non-racing live-camera role.
      if(current->game<2 && s.self!=current->master && current->state==0) {
        if(ImGui::Checkbox("Spectate (live camera) instead of racing",&spectate))
          ImGui::TextDisabled("You will join as a LIVE spectator: no car, you watch the race.");
      }
      ImGui::Spacing();
      ImGui::BeginDisabled(current->state!=0);
      if(ImGui::Button("Leave lobby")) client.Leave();
      ImGui::SameLine();
      ImGui::BeginDisabled(current->master!=s.self||current->members.size()<2||current->hostChecking||
        (current->nativeSettingsSupported && NativeRoomSettings::CarLimit(current->game) && std::any_of(current->members.begin(),current->members.end(),[](const Online::Member& m){return !m.carNumber;})));
      if(ImGui::Button("Start Link")) client.Start(current->id);
      ImGui::EndDisabled();
      ImGui::SameLine();
      if(ImGui::Button("Options")) openRoomOptions=true;
      ImGui::EndDisabled();
      if(current->master!=s.self) ImGui::TextWrapped("Waiting for the master to start. Your game will load automatically.");
      else ImGui::TextWrapped("Start Link loads the selected game on every cabinet, then links once all are connected. Press Enter in-game to keep chatting.");
    } else {
      const uint32_t available=Available(dir);
      // Game selection applies only when creating a lobby.
      if(ImGui::Button("Create Lobby")) openCreateDialog=true;
      if(s.pingMs){ImGui::SameLine();ImGui::TextColored(PingColor(s.pingMs),"your ping %u ms",unsigned(s.pingMs));if(ImGui::IsItemHovered())ImGui::SetTooltip("Round trip to this lobby server, measured by the lobby poll");}
      ImGui::TextUnformatted("Available lobbies (updates automatically)");
      const Online::Room* selected=nullptr;
      const float tableHeight=(std::max)(95.0f*scale,ImGui::GetContentRegionAvail().y-34.0f*scale);
      if(ImGui::BeginTable("Online rooms",4,ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg|ImGuiTableFlags_ScrollY,ImVec2(0,tableHeight))) {
        ImGui::TableSetupColumn("Lobby");ImGui::TableSetupColumn("Game");ImGui::TableSetupColumn("Players");ImGui::TableSetupColumn("Status");ImGui::TableHeadersRow();
        for(const auto& r:s.rooms) {
          ImGui::PushID(int(r.id));ImGui::TableNextRow();ImGui::TableNextColumn();
          if(ImGui::Selectable(r.name.c_str(),selectedRoom==r.id,ImGuiSelectableFlags_SpanAllColumns)) selectedRoom=r.id;
          ImGui::TableNextColumn();ImGui::TextUnformatted(Online::Titles[r.game]);ImGui::TableNextColumn();ImGui::Text("%u/%u",unsigned(r.members.size()),Online::MaxPlayers(r.game));
          ImGui::TableNextColumn();ImGui::TextUnformatted(r.state==0?(r.hostChecking?"Testing host":r.udpMode==3?"Waiting (full room host)":r.udpMode==2?"Waiting (self-host)":r.udpMode==1?"Waiting (legacy UDP)":"Waiting (relay)"):r.state==1?"Launching":r.state==2?"Linked":"Failed");ImGui::PopID();
        }
        ImGui::EndTable();
      }
      for(const auto& r:s.rooms) if(r.id==selectedRoom) selected=&r;
      ImGui::BeginDisabled(!selected||selected->state!=0||selected->members.size()>=Online::MaxPlayers(selected->game)||!(available&(1u<<selected->game)));
      if(ImGui::Button("Join Lobby")) client.Join(selectedRoom);
      ImGui::EndDisabled();
      if(s.rooms.empty()) {ImGui::SameLine();ImGui::TextDisabled("No lobbies yet. Create one.");}
    }

    ImGui::TableSetColumnIndex(1);
    const uint32_t activeScope = current ? current->id : 0u;
    const auto& messages = current ? s.roomChat : s.loungeChat;
    if(current) {
      ImGui::TextUnformatted("Lobby Chat");
      ImGui::SameLine();
      ImGui::TextDisabled("(%s - carries into game)",current->name.c_str());
    } else {
      ImGui::TextUnformatted("Lounge Chat");
      ImGui::SameLine();
      ImGui::TextDisabled("(all lobby browsers)");
    }

    const float footerHeight = ImGui::GetFrameHeightWithSpacing() + 4.0f * scale;
    const float logHeight = (std::max)(110.0f * scale, ImGui::GetContentRegionAvail().y - footerHeight);
    if(ImGui::BeginChild("LobbyChatHistory", ImVec2(0, logHeight), true)) {
      if(messages.empty()) {
        ImGui::TextDisabled("%s", current
          ? "No lobby messages yet. Messages sent here stay visible once the game launches!"
          : "No lounge messages yet. Coordinate which game to host here.");
      } else {
        ImGui::PushTextWrapPos(0.0f);
        for(const auto& m : messages) {
          if(m.car > 0) {
            ImGui::TextColored(CarBadgeColor(m.car), "[Car %u] %s:", unsigned(m.car), m.sender.c_str());
          } else {
            ImGui::TextColored(CarBadgeColor(0), "%s:", m.sender.c_str());
          }
          ImGui::SameLine(0.0f, 6.0f * scale);
          ImGui::TextUnformatted(m.text.c_str());
        }
        ImGui::PopTextWrapPos();
      }
      const uint32_t newestSeq = messages.empty() ? 0u : messages.back().seq;
      if(activeScope != lastChatScope || newestSeq != lastChatSeq) {
        ImGui::SetScrollHereY(1.0f);
        lastChatScope = activeScope;
        lastChatSeq = newestSeq;
      }
    }
    ImGui::EndChild();

    const float sendWidth = 58.0f * scale;
    ImGui::SetNextItemWidth((std::max)(80.0f * scale, ImGui::GetContentRegionAvail().x - sendWidth - ImGui::GetStyle().ItemSpacing.x));
    const bool pressedEnter = ImGui::InputTextWithHint("##LobbyChatInput", "Type message and press Enter...", chatInput, sizeof(chatInput), ImGuiInputTextFlags_EnterReturnsTrue);
    if(refocusChat) {
      ImGui::SetKeyboardFocusHere(-1);
      refocusChat = false;
    }
    ImGui::SameLine();
    const bool canSend = HasPrintableText(chatInput);
    ImGui::BeginDisabled(!canSend);
    const bool clickedSend = ImGui::Button("Send", ImVec2(sendWidth, 0));
    ImGui::EndDisabled();
    if((pressedEnter || clickedSend) && canSend) {
      client.SendChat(chatInput);
      chatInput[0] = '\0';
      refocusChat = true;
    }

    ImGui::EndTable();
  }
  DrawCreateDialog(dir,s,openCreateDialog);
  DrawRoomOptionsDialog(s,openRoomOptions);
}

void OnlineGui::DrawCreateDialog(const std::string& dir,const Online::Snapshot& s,bool requestOpen) {
  // Draw dialogs outside the lobby table.
  if(requestOpen) ImGui::OpenPopup("Create Lobby");
  const float scale=ImGui::GetFontSize()/13.0f;
  ImGui::SetNextWindowSize(ImVec2(490.0f*scale,0),ImGuiCond_Appearing);
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),ImGuiCond_Appearing,ImVec2(0.5f,0.5f));
  bool open=true;
  if(ImGui::BeginPopupModal("Create Lobby",&open,ImGuiWindowFlags_AlwaysAutoResize)) {
    if(!s.connected || s.room || s.launch) {
      ImGui::CloseCurrentPopup();
    } else {
      if(game<0 || unsigned(game)>=Online::GameCount) game=0;
      const uint32_t available=Available(dir);
      ImGui::SetNextItemWidth(300.0f*scale);
      if(ImGui::BeginCombo("Selected ROM",Online::Titles[game])) {
        for(unsigned i=0;i<Online::GameCount;++i) {
          ImGui::BeginDisabled(!(available&(1u<<i)));
          if(ImGui::Selectable(Online::Titles[i],unsigned(game)==i)) game=int(i);
          ImGui::EndDisabled();
        }
        ImGui::EndCombo();
      }
      if(!(available&(1u<<game))) ImGui::TextDisabled("missing %s.zip",Online::RomGame(game));
      ImGui::SetNextItemWidth(300.0f*scale);
      if(ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
      ImGui::InputText("Lobby name",title,sizeof(title));
      ImGui::Separator();
      ImGui::SetNextItemWidth(300.0f*scale);
      ImGui::Combo("Hosting",&udpHosting,"Central relay\0Self-host room + TCP/UDP (try UPnP)\0");
      if(udpHosting) {
        ImGui::SetNextItemWidth(110.0f*scale);
        ImGui::InputInt("Host TCP/UDP port",&udpPort,0,0);
        ImGui::TextWrapped("The creator hosts the room, chat, standings and all TCP/UDP gameplay. Central only lists the room. TCP and UDP are tested at creation; failure switches the room to central relay hosting and announces it in chat. Your IP is shared with players.");
        if(udpPort<1024 || udpPort>65535) ImGui::TextDisabled("Enter a host port from 1024 to 65535.");
      } else {
        ImGui::TextWrapped("The central relay hosts the room, chat and gameplay.");
      }
      ImGui::Spacing();
      const bool canCreate=game>=0 && unsigned(game)<Online::GameCount && (Available(dir)&(1u<<game)) && HasPrintableText(title) && (!udpHosting || (udpPort>=1024 && udpPort<=65535));
      ImGui::BeginDisabled(!canCreate);
      if(ImGui::Button("Create",ImVec2(110.0f*scale,0))) {
        client.Create(uint32_t(game),title,udpHosting!=0,uint16_t(udpPort));
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndDisabled();
      ImGui::SameLine();
      if(ImGui::Button("Cancel",ImVec2(110.0f*scale,0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

void OnlineGui::DrawRoomOptionsDialog(const Online::Snapshot& s,bool requestOpen) {
  // Room options are authoritative on the server.
  if(requestOpen && s.connected && s.room && !s.launch) {
    optionsRoom=s.room;
    ImGui::OpenPopup("Room Options");
  }
  const float scale=ImGui::GetFontSize()/13.0f;
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),ImGuiCond_Appearing,ImVec2(0.5f,0.5f));
  bool open=true;
  if(ImGui::BeginPopupModal("Room Options",&open,ImGuiWindowFlags_AlwaysAutoResize)) {
    if(!s.connected || !s.room || s.room!=optionsRoom || s.launch) {
      ImGui::CloseCurrentPopup();
    } else {
      const auto room=std::find_if(s.rooms.begin(),s.rooms.end(),[&](const Online::Room& r){return r.id==s.room;});
      if(room!=s.rooms.end()) {
        const bool frozen=room->state!=0 || room->hostChecking;
        if(NativeRoomSettings::Supported(room->game)){
          ImGui::TextUnformatted("Shared native game settings");
          ImGui::BeginDisabled(room->master!=s.self || frozen || !room->nativeSettingsSupported);
          int difficulty=int(NativeRoomSettings::Difficulty(room->options)),mode=int(NativeRoomSettings::Mode(room->options));
          ImGui::SetNextItemWidth(240.0f*scale);
          if(ImGui::BeginCombo("Difficulty",NativeRoomSettings::DifficultyName(room->game,unsigned(difficulty)))){
            for(unsigned v=0;v<NativeRoomSettings::DifficultyCount(room->game);++v)
              if(ImGui::Selectable(NativeRoomSettings::DifficultyName(room->game,v),unsigned(difficulty)==v))client.SetRoomOptions(room->id,NativeRoomSettings::WithDifficulty(room->options,v));
            ImGui::EndCombo();
          }
          ImGui::SetNextItemWidth(240.0f*scale);
          if(ImGui::BeginCombo("Game mode",NativeRoomSettings::ModeName(room->game,unsigned(mode)))){
            for(unsigned v=0;v<NativeRoomSettings::ModeCount(room->game);++v)
              if(ImGui::Selectable(NativeRoomSettings::ModeName(room->game,v),unsigned(mode)==v))client.SetRoomOptions(room->id,NativeRoomSettings::WithMode(room->options,v));
            ImGui::EndCombo();
          }
          ImGui::EndDisabled();
          if(room->master!=s.self)ImGui::TextDisabled("Only the room master can change these settings.");
          if(!room->nativeSettingsSupported)ImGui::TextDisabled("Update the relay and every client for native settings.");
          if(!(room->options&NativeRoomSettings::Enabled))ImGui::TextDisabled("Shared native settings have not been enabled in this room.");
          ImGui::Spacing();
        }
        if(NativeRoomSettings::CarLimit(room->game)){
          ImGui::Separator();ImGui::TextUnformatted("Your native CAR NUMBER");
          const auto self=std::find_if(room->members.begin(),room->members.end(),[&](const Online::Member& m){return m.id==s.self;});
          const auto car=self==room->members.end()?0u:self->carNumber;
          const std::string label=car?std::to_string(car):"Not selected";
          ImGui::BeginDisabled(frozen || !room->nativeSettingsSupported || self==room->members.end());
          ImGui::SetNextItemWidth(240.0f*scale);
          if(ImGui::BeginCombo("CAR NUMBER",label.c_str())){
            if(ImGui::Selectable("Not selected",car==0))client.SetCarNumber(room->id,0);
            for(unsigned v=1;v<=NativeRoomSettings::CarLimit(room->game);++v){
              const auto owner=std::find_if(room->members.begin(),room->members.end(),[&](const Online::Member& m){return m.id!=s.self && m.carNumber==v;});
              const bool taken=owner!=room->members.end();
              const auto name=std::to_string(v)+(taken?" - "+owner->name:"");
              ImGui::BeginDisabled(taken);
              if(ImGui::Selectable(name.c_str(),car==v))client.SetCarNumber(room->id,v);
              ImGui::EndDisabled();
            }
            ImGui::EndCombo();
          }
          ImGui::EndDisabled();
          ImGui::TextDisabled("Each player chooses their own number. No duplicates.");
          ImGui::TextDisabled("To exchange occupied numbers, release one with Not selected.");
          ImGui::TextDisabled("Everyone must select a number before Start Link.");
          ImGui::Spacing();
        }
        if(room->game==0){
          bool hornet=(room->options&NativeRoomSettings::Hornet)!=0;
          ImGui::BeginDisabled(room->master!=s.self || frozen || !room->optionsSupported);
          if(ImGui::Checkbox("Enable Hornet (hold Brake)",&hornet))client.SetRoomOptions(room->id,(room->options&~NativeRoomSettings::Hornet)|(hornet?NativeRoomSettings::Hornet:0u));
          ImGui::EndDisabled();
          ImGui::TextDisabled("Hold the brake pedal while confirming a course for native Hornet selection/tuning.");
          ImGui::TextDisabled("Master only; update every client and relay for this option.");
        }
        if(room->game==3){
          bool secretCars=(room->options&1)!=0;
          ImGui::BeginDisabled(room->master!=s.self || frozen || !room->optionsSupported);
          if(ImGui::Checkbox("Enable secret cars",&secretCars))client.SetRoomOptions(room->id,(room->options&~1u)|(secretCars?1u:0u));
          ImGui::EndDisabled();
        }
        if(room->game<4){
          bool realPlayers=(room->options&NativeRoomSettings::RealPlayers)!=0;
          ImGui::BeginDisabled(room->master!=s.self || frozen || !room->optionsSupported);
          if(ImGui::Checkbox("Real players only (no CPU cars)",&realPlayers))client.SetRoomOptions(room->id,(room->options&~NativeRoomSettings::RealPlayers)|(realPlayers?NativeRoomSettings::RealPlayers:0u));
          ImGui::EndDisabled();
          ImGui::TextDisabled("Grid contains only the cabinets in this room; playing solo starts time attack.");
          bool noCollisions=(room->options&NativeRoomSettings::NoCollisions)!=0;
          ImGui::BeginDisabled(room->master!=s.self || frozen || !room->optionsSupported);
          if(ImGui::Checkbox("Disable car collisions (no crashes)",&noCollisions))client.SetRoomOptions(room->id,(room->options&~NativeRoomSettings::NoCollisions)|(noCollisions?NativeRoomSettings::NoCollisions:0u));
          ImGui::EndDisabled();
          ImGui::TextDisabled("All cars ghost through each other; walls still collide.");
          ImGui::TextDisabled("Master only; update every client and relay for these options.");
        }
        if(NativeRoomSettings::Supported(room->game))ImGui::TextDisabled("Applied at launch for this online session only. Offline NVRAM is unchanged.");
      } else ImGui::Dummy(ImVec2(320.0f*scale,70.0f*scale));
      if(ImGui::Button("Close",ImVec2(110.0f*scale,0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}
