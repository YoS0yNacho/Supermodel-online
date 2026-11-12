// Nonblocking IPv4 lobby & room-chat transport; Windows/POSIX, no extra dependencies. GPL-3.0-or-later.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#endif
#include "LobbyClient.h"
#include "PlayerHost.h"
#include "Network/RelayProtocol.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
namespace Online {
namespace {
using Clock = std::chrono::steady_clock;
#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket Bad = INVALID_SOCKET;
void Close(Socket s) { closesocket(s); }
#else
using Socket = int;
constexpr Socket Bad = -1;
void Close(Socket s) { close(s); }
#endif
struct Route {std::string host;uint16_t port=0;std::array<uint8_t,16> key{};bool native=false;};
struct SocketOwner { Socket socket = Bad; ~SocketOwner() { if (socket != Bad) Close(socket); } };
void Put(std::vector<uint8_t>& b, uint32_t x) { for (int i=0;i<4;++i) b.push_back(uint8_t(x>>(8*i))); }
uint32_t U32(const std::vector<uint8_t>& b, size_t p) {
  if (p+4>b.size()) throw std::runtime_error("Malformed lobby response");
  return uint32_t(b[p]) | (uint32_t(b[p+1])<<8) | (uint32_t(b[p+2])<<16) | (uint32_t(b[p+3])<<24);
}
void Text(std::vector<uint8_t>& b, const std::string& s, size_t n) {
  const auto start=b.size(); b.resize(start+n,0);
  for (size_t i=0;i<std::min(n-1,s.size());++i) {
    const unsigned char ch = static_cast<unsigned char>(s[i]);
    b[start+i]=(ch>=32&&ch<127)?ch:uint8_t('?');
  }
}
std::string String(const std::vector<uint8_t>& b,size_t p,size_t n) {
  if(p+n>b.size()) throw std::runtime_error("Malformed lobby text");
  std::string s;
  for(size_t i=0;i<n && b[p+i];++i) s+=b[p+i]>=32&&b[p+i]<127?char(b[p+i]):'?';
  return s;
}
size_t Utf8SeqLen(const uint8_t* d, size_t rem) {
  if (rem == 0) return 0;
  const uint8_t c = d[0];
  if (c < 0x80) return 1;
  if (c >= 0xC2 && c <= 0xDF) {
    if (rem >= 2 && (d[1] & 0xC0) == 0x80) return 2;
    return 0;
  }
  if (c >= 0xE0 && c <= 0xEF) {
    if (rem >= 3 && (d[1] & 0xC0) == 0x80 && (d[2] & 0xC0) == 0x80) {
      if (c == 0xE0 && d[1] < 0xA0) return 0;
      if (c == 0xED && d[1] >= 0xA0) return 0;
      return 3;
    }
    return 0;
  }
  if (c >= 0xF0 && c <= 0xF4) {
    if (rem >= 4 && (d[1] & 0xC0) == 0x80 && (d[2] & 0xC0) == 0x80 && (d[3] & 0xC0) == 0x80) {
      if (c == 0xF0 && d[1] < 0x90) return 0;
      if (c == 0xF4 && d[1] >= 0x90) return 0;
      return 4;
    }
    return 0;
  }
  return 0;
}
std::string SanitizeChat(const uint8_t* raw, size_t len, size_t maxBytes) {
  std::string out;
  out.reserve(std::min(len, maxBytes));
  size_t i = 0;
  while (i < len && raw[i] != 0) {
    const uint8_t c = raw[i];
    if (c == '\t' || c == '\r' || c == '\n') {
      if (out.size() + 1 > maxBytes) break;
      out.push_back(' ');
      ++i;
      continue;
    }
    if (c < 32 || c == 127) {
      ++i;
      continue;
    }
    const size_t seq = Utf8SeqLen(raw + i, len - i);
    if (seq == 0) {
      if (out.size() + 1 > maxBytes) break;
      out.push_back('?');
      ++i;
    } else {
      if (out.size() + seq > maxBytes) break;
      out.append(reinterpret_cast<const char*>(raw + i), seq);
      i += seq;
    }
  }
  while (!out.empty() && out.front() == ' ') out.erase(out.begin());
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}
void PutChatText(std::vector<uint8_t>& b, const std::string& s, size_t n) {
  const auto clean = SanitizeChat(reinterpret_cast<const uint8_t*>(s.data()), s.size(), n > 0 ? n - 1 : 0);
  const auto start = b.size();
  b.resize(start + n, 0);
  if (!clean.empty()) std::memcpy(b.data() + start, clean.data(), clean.size());
}
std::string ReadChatString(const std::vector<uint8_t>& b, size_t p, size_t n) {
  if (p + n > b.size()) throw std::runtime_error("Malformed chat text");
  size_t actual = 0;
  while (actual < n && b[p + actual] != 0) ++actual;
  return SanitizeChat(b.data() + p, actual, n > 0 ? n - 1 : 0);
}
void AppendChatUnique(std::vector<ChatMessage>& list, ChatMessage msg, size_t maxKeep) {
  for (const auto& existing : list) {
    if (existing.scope == msg.scope && existing.seq == msg.seq) return;
  }
  list.push_back(std::move(msg));
  if (list.size() > maxKeep) list.erase(list.begin(), list.begin() + static_cast<std::ptrdiff_t>(list.size() - maxKeep));
}
std::vector<uint8_t> Frame(const char* id,const std::vector<uint8_t>& b={}) {
  std::vector<uint8_t> f; Put(f,uint32_t(b.size()+8)); f.insert(f.end(),id,id+4); f.insert(f.end(),b.begin(),b.end()); return f;
}
void Require(bool good) { if(!good) throw std::runtime_error("Invalid/incompatible lobby response; update the relay"); }
bool WouldBlock() {
#ifdef _WIN32
  const int e=WSAGetLastError(); return e==WSAEWOULDBLOCK||e==WSAEINPROGRESS||e==WSAEINTR;
#else
  return errno==EWOULDBLOCK||errno==EAGAIN||errno==EINPROGRESS||errno==EINTR;
#endif
}
bool OpenNonblockingSocket(const std::atomic<bool>& stop, const std::string& host, uint16_t port, SocketOwner& owner) {
  addrinfo hints{}; hints.ai_family=AF_INET; hints.ai_socktype=SOCK_STREAM;
  addrinfo* addresses=nullptr;
  if(getaddrinfo(host.c_str(),std::to_string(port).c_str(),&hints,&addresses)!=0) throw std::runtime_error("Relay hostname could not be resolved");
  std::unique_ptr<addrinfo,decltype(&freeaddrinfo)> addressOwner(addresses,freeaddrinfo);
  if(stop) return false;
  owner.socket=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
  Socket fd=owner.socket;
  if(fd==Bad) throw std::runtime_error("Cannot create relay socket");
#ifndef _WIN32
  if(fd>=FD_SETSIZE)throw std::runtime_error("Too many open sockets");
#endif
#ifdef _WIN32
  u_long nonblocking=1; if(ioctlsocket(fd,FIONBIO,&nonblocking)) throw std::runtime_error("Cannot set nonblocking socket");
#else
  if(fcntl(fd,F_SETFL,O_NONBLOCK)<0) throw std::runtime_error("Cannot set nonblocking socket");
#ifdef SO_NOSIGPIPE
  int noSig=1; setsockopt(fd,SOL_SOCKET,SO_NOSIGPIPE,&noSig,sizeof(noSig));
#endif
#endif
  int result=connect(fd,addresses->ai_addr,int(addresses->ai_addrlen));
  if(result!=0 && !WouldBlock()) throw std::runtime_error("Cannot connect to relay; check IP, port and firewall");
  auto deadline=Clock::now()+std::chrono::seconds(8);
  while(result!=0) {
    if(stop) return false;
    fd_set write; FD_ZERO(&write); FD_SET(fd,&write); timeval tv{0,100000};
    int ready=select(int(fd+1),nullptr,&write,nullptr,&tv);
    if(ready<0 && !WouldBlock()) throw std::runtime_error("Relay connection failed");
    if(ready>0) {
      int error=0;
#ifdef _WIN32
      int size=sizeof(error);
#else
      socklen_t size=sizeof(error);
#endif
      if(getsockopt(fd,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&error),&size)!=0||error) throw std::runtime_error("Cannot connect to relay; check IP, port and firewall");
      break;
    }
    if(Clock::now()>=deadline) throw std::runtime_error("Relay connection timed out");
  }
  return true;
}
}

struct LobbyClient::State {
  std::atomic<bool> stop{false},back{false};
  std::mutex mutex;
  Snapshot view;
  std::deque<std::vector<uint8_t>> outgoing;
};
LobbyClient::LobbyClient():state(std::make_shared<State>()) {}
LobbyClient::~LobbyClient() { Disconnect(); }
void LobbyClient::Disconnect() {
  state->stop=true;
  state=std::make_shared<State>();
}
Snapshot LobbyClient::Get() const { std::lock_guard<std::mutex> lock(state->mutex); return state->view; }
void LobbyClient::Connect(const std::string& endpoint,const std::string& name,uint32_t available,bool enabled) {
  Disconnect();
  std::string host; uint16_t port;
  if(!RelayProtocol::ParseEndpoint(endpoint,host,port)) { state->view.error="Enter an IPv4 address or hostname and port (example 192.168.1.10:1970)."; return; }
  state->view.connecting=true; state->view.status="Connecting..."; state->view.endpoint=endpoint; state->view.directoryEndpoint=endpoint;
  // The worker captures only shared state, never this or GUI objects. Disconnect
  // cancels IO within 100 ms; an OS DNS resolver may finish later without touching UI.
  std::thread(&LobbyClient::Worker,state,host,port,name,available,enabled).detach();
}
void LobbyClient::Queue(const char* id,std::vector<uint8_t> body) {
  std::lock_guard<std::mutex> lock(state->mutex);
  if(state->view.connected && !state->view.launch && state->outgoing.size()<16) {
    state->outgoing.push_back(Frame(id,body)); state->view.error.clear();
  }
}
void LobbyClient::Create(uint32_t game,const std::string& title,bool selfHostUdp,uint16_t udpPort) { std::vector<uint8_t> b; Put(b,game); Text(b,title,64); Put(b,selfHostUdp?3:0); Put(b,selfHostUdp?udpPort:0); Queue("OLC4",b); }
void LobbyClient::Join(uint32_t room) { std::vector<uint8_t> b; Put(b,room); Queue("OLJ1",b); }
void LobbyClient::Leave() { bool native;{std::lock_guard<std::mutex> l(state->mutex);native=state->view.hostedRoom;}if(native)state->back=true;else Queue("OLL1"); }
void LobbyClient::Start(uint32_t room) { std::vector<uint8_t> b; Put(b,room); Queue("OLT1",b); }
void LobbyClient::SetRoomOptions(uint32_t room,uint32_t options) { std::vector<uint8_t> b; Put(b,room); Put(b,options); Queue("ORO1",b); }
void LobbyClient::SetCarNumber(uint32_t room,uint32_t carNumber) { std::vector<uint8_t> b; Put(b,room); Put(b,carNumber); Queue("OCN1",b); }
void LobbyClient::SendChat(const std::string& message) {
  const auto clean = SanitizeChat(reinterpret_cast<const uint8_t*>(message.data()), message.size(), MaxChatLength);
  if (clean.empty()) return;
  std::vector<uint8_t> b;
  PutChatText(b, clean, 160);
  Queue("OLS1", std::move(b));
}
void LobbyClient::Worker(std::shared_ptr<State> s,std::string serverHost,uint16_t port,std::string name,uint32_t available,bool enabled) {
#ifdef _WIN32
  WSADATA data;
  if(WSAStartup(MAKEWORD(2,2),&data)!=0) { std::lock_guard<std::mutex> l(s->mutex);s->view.connecting=false;s->view.error="Winsock initialization failed";return; }
  struct Cleanup { ~Cleanup() { WSACleanup(); } } cleanup;
#endif
  Route directory{serverHost,port,{},false},route=directory;
  std::shared_ptr<PlayerHost> host;std::vector<std::shared_ptr<PlayerHost>> retiredHosts;
  uint32_t hostRoom=0;
  for(;;) try {
    SocketOwner owner;
    if(!OpenNonblockingSocket(s->stop,route.host,route.port,owner)) return;
    Socket fd=owner.socket;
    std::vector<uint8_t> hello; Put(hello,1); Put(hello,available&Online::GameMaskAll); Put(hello,(enabled?1u:0u)|54u); Text(hello,name,32);
    std::vector<uint8_t> output=Frame("HEL1",{'S','u','p','e','r','m','o','d','e','l',' ','O','n','l','i','n','e'});
    if(route.native)hello.insert(hello.end(),route.key.begin(),route.key.end());
    const auto registration=Frame(Srally2Research::LobbyHello,hello);output.insert(output.end(),registration.begin(),registration.end());
    std::vector<uint8_t> input; size_t sent=0;
    bool bindPending=false,armPending=false;
    Snapshot pending; size_t expected=0; bool collecting=false, greeted=false, admitted=false;
    auto received=Clock::now(),polled=Clock::now(),sendProgress=Clock::now();
    while(!s->stop) {
      retiredHosts.erase(std::remove_if(retiredHosts.begin(),retiredHosts.end(),[](const auto& h){return h->Finished();}),retiredHosts.end());
      if(route.native && s->back.exchange(false)){
        if(host){host->Stop();retiredHosts.push_back(host);host.reset();}
        {std::lock_guard<std::mutex> l(s->mutex);s->outgoing.clear();s->view=Snapshot{};s->view.endpoint=serverHost+":"+std::to_string(port);s->view.directoryEndpoint=s->view.endpoint;s->view.connecting=true;}
        throw directory;
      }
      if(host && ((bindPending && host->Booted()) || (armPending && (host->Armed() || host->Status()==3)))) {
        std::vector<uint8_t> body;Put(body,hostRoom);Put(body,bindPending?unsigned(host->Bound()):unsigned(host->Armed()));
        std::lock_guard<std::mutex> lock(s->mutex);s->outgoing.push_back(Frame(bindPending?"OHB1":"OHA1",body));bindPending=armPending=false;
      }
      {
        std::lock_guard<std::mutex> lock(s->mutex);
        if(sent==output.size()) {
          output.clear(); sent=0;
          if(!s->outgoing.empty()) { output=std::move(s->outgoing.front());s->outgoing.pop_front();sendProgress=Clock::now(); }
          else if(admitted && Clock::now()-polled>std::chrono::seconds(2)) { output=Frame("OLQ1");polled=Clock::now();sendProgress=Clock::now(); }
        }
      }
      fd_set read,write;FD_ZERO(&read);FD_ZERO(&write);FD_SET(fd,&read);
      if(sent<output.size()) FD_SET(fd,&write);
      timeval tv{0,100000}; int ready=select(int(fd+1),&read,&write,nullptr,&tv);
      if(ready<0) { if(WouldBlock()) continue; throw std::runtime_error("Relay socket error"); }
      if(FD_ISSET(fd,&write)) {
#ifdef MSG_NOSIGNAL
        constexpr int flags=MSG_NOSIGNAL;
#else
        constexpr int flags=0;
#endif
        int n=send(fd,reinterpret_cast<const char*>(output.data()+sent),int(output.size()-sent),flags);
        if(n>0) { sent+=size_t(n);sendProgress=Clock::now(); }
        else if(n==0||!WouldBlock()) throw std::runtime_error("Relay send failed");
      }
      if(FD_ISSET(fd,&read)) {
        uint8_t bytes[2048]; int n=recv(fd,reinterpret_cast<char*>(bytes),sizeof(bytes),0);
        if(n==0) throw std::runtime_error("Relay disconnected; update relay if it does not support Online lobbies");
        if(n<0) { if(!WouldBlock()) throw std::runtime_error("Relay receive failed"); }
        else { input.insert(input.end(),bytes,bytes+n);received=Clock::now(); }
      }
      while(input.size()>=8) {
        const uint32_t size=U32(input,0);Require(size>=8&&size<=1032);
        if(input.size()<size) break;
        const std::string kind(input.begin()+4,input.begin()+8);
        const std::vector<uint8_t> b(input.begin()+8,input.begin()+size);input.erase(input.begin(),input.begin()+size);
        if(kind=="HEL1") { Require(!greeted);greeted=true;continue; }
        Require(greeted);
        if(kind=="OLB1") {
          Require(b.size()==12&&!collecting);expected=U32(b,0);Require(expected<=16);
          pending=Snapshot{};pending.self=U32(b,4);pending.room=U32(b,8);Require(pending.self!=0);collecting=true;
        } else if(kind=="OLR1") {
          Require(collecting&&pending.rooms.size()<expected&&b.size()>=84);
          Room r; r.id=U32(b,0);r.game=U32(b,4);r.state=U32(b,8);uint32_t count=U32(b,12);r.master=U32(b,16);r.name=String(b,20,64);
          Require(r.id&&r.game<Online::GameCount&&r.state<=3&&count>=1&&count<=Online::MaxPlayers(r.game)&&b.size()==84+count*36);
          for(uint32_t i=0;i<count;++i) r.members.push_back({U32(b,84+i*36),String(b,88+i*36,32)});
          Require(r.master==r.members[0].id);pending.rooms.push_back(std::move(r));
        } else if(kind=="OHP1") {
          Require(admitted && !collecting && b.size()==24 && !host);
          hostRoom=U32(b,0);auto port=U32(b,4);Require(port>=1024 && port<=65535);
          std::array<uint8_t,16> probe{};std::memcpy(probe.data(),b.data()+8,16);
          host=std::make_shared<PlayerHost>(uint16_t(port),probe);bindPending=true;
          std::lock_guard<std::mutex> lock(s->mutex);Require(s->view.room==hostRoom);s->view.playerHost=host;
        } else if(kind=="OHD1") {
          Require(!route.native && admitted && !collecting && b.size()==100);
          Route next;next.native=true;next.host=std::to_string(b[16])+"."+std::to_string(b[17])+"."+std::to_string(b[18])+"."+std::to_string(b[19]);auto port=U32(b,12);Require(port>=1024&&port<=65535);next.port=uint16_t(port);std::memcpy(next.key.data(),b.data()+84,16);
          if(host){
            Require(U32(b,0)==hostRoom);PlayerRoom::Settings settings;settings.id=hostRoom;settings.game=U32(b,4);settings.tick=U32(b,8);settings.port=next.port;settings.title=String(b,20,64);settings.owner=next.key;
#ifdef _WIN32
            int length=sizeof(settings.directory);
#else
            socklen_t length=sizeof(settings.directory);
#endif
            Require(getpeername(fd,reinterpret_cast<sockaddr*>(&settings.directory),&length)==0 && host->HostRoom(settings));
          }
          {std::lock_guard<std::mutex> l(s->mutex);s->outgoing.clear();s->view.rooms.clear();s->view.room=0;s->view.roomChat.clear();s->view.connected=false;s->view.connecting=true;s->view.endpoint=next.host+":"+std::to_string(next.port);s->view.status="Connecting to creator room...";}
          throw next;
        } else if(kind=="OHS1") {
          Require(b.size()==4);
          if(host && U32(b,0)==hostRoom){host->Stop();retiredHosts.push_back(host);host.reset();bindPending=armPending=false;std::lock_guard<std::mutex> lock(s->mutex);s->view.playerHost.reset();}
        } else if(kind=="OHR1") {
          Require(host && b.size()>=16 && U32(b,0)==hostRoom);
          PlayerHost::Config config;config.game=U32(b,4);config.tick=U32(b,8);auto count=U32(b,12);Require(count>=2 && count<=16 && b.size()==16+32*count);
          for(uint32_t i=0;i<count;++i){PlayerHost::Member m;std::memcpy(m.ticket.data(),b.data()+16+32*i,16);std::memcpy(m.cookie.data(),b.data()+32+32*i,16);config.members.push_back(m);}
          if(host->Configure(config))armPending=true;
          else {std::vector<uint8_t> body;Put(body,hostRoom);Put(body,0);std::lock_guard<std::mutex> lock(s->mutex);s->outgoing.push_back(Frame("OHA1",body));}
        } else if(kind=="ORO1") {
          Require(collecting && !pending.rooms.empty() && b.size()==8);
          auto& room=pending.rooms.back();Require(U32(b,0)==room.id && !room.optionsSupported);
          room.options=U32(b,4);Require(NativeRoomSettings::Valid(room.game,room.options));room.optionsSupported=true;
        } else if(kind=="ORN1") {
          Require(collecting && !pending.rooms.empty() && b.size()>=12);
          auto& room=pending.rooms.back();const auto count=U32(b,8);
          Require(U32(b,0)==room.id && U32(b,4)<=1 && !room.nativeSettingsReceived && count==room.members.size() && b.size()==12+8*count);
          uint32_t used=0;
          for(unsigned i=0;i<count;++i){const auto car=U32(b,16+8*i);Require(U32(b,12+8*i)==room.members[i].id && car<=NativeRoomSettings::CarLimit(room.game));
            if(car){Require(!(used&(1u<<(car-1))));used|=1u<<(car-1);}room.members[i].carNumber=car;}
          room.nativeSettingsReceived=true;room.nativeSettingsSupported=U32(b,4)!=0;
        } else if(kind=="OLU2") {
          Require(collecting && !pending.rooms.empty() && b.size()==16);auto& room=pending.rooms.back();Require(U32(b,0)==room.id);
          room.udpPort=U32(b,4);room.udpMode=U32(b,8);room.hostChecking=U32(b,12)!=0;
          Require(room.udpMode<=3 && U32(b,12)<=1 && (room.udpMode ? room.udpPort>=1024&&room.udpPort<=65535 : room.udpPort==0));
        } else if(kind=="OLU1") {
          Require(collecting && !pending.rooms.empty() && b.size()==12);
          auto& room=pending.rooms.back();Require(U32(b,0)==room.id);
          room.udpPort=U32(b,4);room.udpMode=U32(b,8);
          Require(room.udpMode<=1 && (room.udpMode ? room.udpPort>=1024&&room.udpPort<=65535 : room.udpPort==0));
        } else if(kind=="OLZ1") {
          Require(b.empty()&&collecting&&pending.rooms.size()==expected); collecting=false;admitted=true;
          std::lock_guard<std::mutex> l(s->mutex);
          if(s->view.room!=pending.room) s->view.roomChat.clear();
          s->view.rooms=std::move(pending.rooms);s->view.self=pending.self;s->view.room=pending.room;
          s->view.hostedRoom=route.native;s->view.connected=true;s->view.connecting=false;s->view.status=route.native?"Connected to creator (central: discovery only)":"Connected";
        } else if(kind=="OLM1") {
          Require(b.size()==204&&!collecting);
          ChatMessage msg;
          msg.seq=U32(b,0); msg.scope=U32(b,4); msg.car=U32(b,8);
          msg.sender=ReadChatString(b,12,32); msg.text=ReadChatString(b,44,160);
          if(!msg.text.empty()) {
            std::lock_guard<std::mutex> l(s->mutex);
            if(msg.scope==0) AppendChatUnique(s->view.loungeChat,std::move(msg),64);
            else if(msg.scope==s->view.room) AppendChatUnique(s->view.roomChat,std::move(msg),64);
          }
        } else if(kind=="OLE1") {
          Require(b.size()==160);std::lock_guard<std::mutex> l(s->mutex);s->view.error=String(b,0,160);
        } else if(kind=="OLG1" || kind=="OLG2" || kind=="OLG3" || kind=="OLG4" || kind=="OLG5" || kind=="OLG6") {
          const bool withNative=kind=="OLG5" || kind=="OLG6";
          const bool withOptions=withNative || kind=="OLG3" || kind=="OLG4", withHost=kind=="OLG2" || kind=="OLG4" || kind=="OLG6";
          Require(b.size()==(withHost?40u:32u)+(withNative?8u:withOptions?4u:0u)&&admitted&&enabled);uint32_t game=U32(b,4),index=U32(b,8),count=U32(b,12);
          const uint32_t roomOptions=withOptions?U32(b,withHost?40:32):0, carNumber=withNative?U32(b,withHost?44:36):0;
          Require(NativeRoomSettings::Valid(game,roomOptions) && carNumber<=NativeRoomSettings::CarLimit(game));
          Require(game<Online::GameCount&&(available&(1u<<game))&&count>=2&&count<=Online::MaxPlayers(game)&&index<count);
          std::string ticket;static constexpr char hex[]="0123456789abcdef";
          bool nonzero=false;for(size_t i=16;i<32;++i) { ticket+=hex[b[i]>>4];ticket+=hex[b[i]&15];nonzero|=b[i]!=0; } Require(nonzero);
          std::lock_guard<std::mutex> l(s->mutex);
          Require(!s->view.launch&&s->view.room==U32(b,0));
          const auto room=std::find_if(s->view.rooms.begin(),s->view.rooms.end(),[&](const Room& r){return r.id==s->view.room;});
          Require(room!=s->view.rooms.end()&&room->game==game&&room->members.size()==count&&room->members[index].id==s->view.self);
          Require(room->options==roomOptions && (!withOptions || room->optionsSupported));
          Require(!(roomOptions&NativeRoomSettings::Enabled) || (withNative && room->nativeSettingsSupported));
          if(withNative){Require(room->nativeSettingsReceived);if(room->nativeSettingsSupported){
            Require(carNumber==room->members[index].carNumber && (game>=4 || carNumber));
          }else Require(carNumber==0 && !(roomOptions&NativeRoomSettings::Enabled));}
          if(withHost) {
            auto port=U32(b,36);Require(port>=1024&&port<=65535);
            s->view.gameEndpoint=std::to_string(b[32])+"."+std::to_string(b[33])+"."+std::to_string(b[34])+"."+std::to_string(b[35])+":"+std::to_string(port);
          } else s->view.gameEndpoint=s->view.endpoint;
          s->view.roomOptions=roomOptions;s->view.carNumber=carNumber;s->view.launch=true;s->view.game=game;s->view.index=index;s->view.count=count;s->view.ticket=ticket;s->view.status="Master started the link. Loading game...";
        } else throw std::runtime_error("Relay does not support this Online protocol; update it");
      }
      if(input.size()>8192) throw std::runtime_error("Oversized lobby response");
      if(Clock::now()-received>std::chrono::seconds(admitted?15:8)) throw std::runtime_error("Relay response timed out");
      if(sent<output.size()&&Clock::now()-sendProgress>std::chrono::seconds(8)) throw std::runtime_error("Relay send timed out");
    }
    return;
  } catch(const Route& next){route=next;continue;} catch(const std::exception& e) {
    if(!s->stop) {std::lock_guard<std::mutex> l(s->mutex);s->view.connecting=false;s->view.connected=false;s->view.error=e.what();s->view.status="Disconnected";}
    return;
  }
}

struct RoomChatClient::State {
  std::atomic<bool> stop{false};
  std::mutex mutex;
  RoomChatSnapshot view;
  NativeStanding standing;
  std::shared_ptr<PlayerHost> host;
  std::deque<std::vector<uint8_t>> outgoing;
};
RoomChatClient::RoomChatClient():state(std::make_shared<State>()) {}
RoomChatClient::~RoomChatClient() { Disconnect(); }
void RoomChatClient::Disconnect() {
  state->stop=true;
  state=std::make_shared<State>();
}
RoomChatSnapshot RoomChatClient::Get() const { std::lock_guard<std::mutex> lock(state->mutex); return state->view; }
void RoomChatClient::SetStanding(NativeStanding standing) {
  std::lock_guard<std::mutex> lock(state->mutex);
  state->standing = StandingReport(standing); // coalesced valid wire report, including unknown rank
}
void RoomChatClient::Connect(const std::string& endpoint, const std::string& ticketHex, const std::string& fallbackName, std::shared_ptr<PlayerHost> playerHost) {
  Disconnect();
  std::string host; uint16_t port = 0;
  uint8_t rawTicketArr[16]{};
  if (!RelayProtocol::ParseEndpoint(endpoint, host, port) || !RelayProtocol::ParseTicket(ticketHex, rawTicketArr)) {
    state->view.error = "Invalid relay chat endpoint or session ticket.";
    state->view.closed = true;
    return;
  }
  std::array<uint8_t, 16> rawTicket{};
  std::copy_n(rawTicketArr, 16, rawTicket.begin());
  state->view.connecting = true;
  state->view.status = "Connecting chat...";
  state->host=std::move(playerHost);
  state->view.player = fallbackName;
  std::thread(&RoomChatClient::Worker, state, host, port, rawTicket, fallbackName).detach();
}
void RoomChatClient::SendChat(const std::string& message) {
  const auto clean = SanitizeChat(reinterpret_cast<const uint8_t*>(message.data()), message.size(), MaxChatLength);
  if (clean.empty()) return;
  std::vector<uint8_t> b;
  PutChatText(b, clean, 160);
  std::lock_guard<std::mutex> lock(state->mutex);
  if (!state->view.closed && state->outgoing.size() < 16) {
    state->outgoing.push_back(Frame("OLS1", b));
    state->view.error.clear();
  }
}
void RoomChatClient::Worker(std::shared_ptr<State> s, std::string host, uint16_t port,
                            std::array<uint8_t, 16> ticket, std::string fallbackName) {
#ifdef _WIN32
  WSADATA data;
  if (WSAStartup(MAKEWORD(2,2), &data) != 0) {
    std::lock_guard<std::mutex> l(s->mutex);
    s->view.connecting = false; s->view.error = "Winsock initialization failed";
    return;
  }
  struct Cleanup { ~Cleanup() { WSACleanup(); } } cleanup;
#endif
  for (int attempt = 0; attempt < 5 && !s->stop; ++attempt) {
    if (attempt > 0) {
      for (int waitSlice = 0; waitSlice < 20 && !s->stop; ++waitSlice)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      if (s->stop) return;
      std::lock_guard<std::mutex> l(s->mutex);
      if (s->view.closed) return;
      s->view.connecting = true;
      s->view.connected = false;
      s->view.status = "Reconnecting chat...";
    }
    try {
      SocketOwner owner;
      if (!OpenNonblockingSocket(s->stop, host, port, owner)) return;
      Socket fd = owner.socket;
      std::vector<uint8_t> join;
      Put(join, 1);
      join.insert(join.end(), ticket.begin(), ticket.end());
      PutChatText(join, fallbackName.empty() ? "Player" : fallbackName, 32);
      std::vector<uint8_t> output = Frame("HEL1", {'S','u','p','e','r','m','o','d','e','l',' ','C','h','a','t'});
      const auto reg = Frame("RLC1", join);
      output.insert(output.end(), reg.begin(), reg.end());
      std::vector<uint8_t> input;
      size_t sent = 0;
      bool greeted = false, admitted = false;
      auto hosted=Clock::now()-std::chrono::seconds(1);
      auto ranked = Clock::now() - std::chrono::seconds(1);
      auto received = Clock::now(), pinged = Clock::now(), sendProgress = Clock::now();
      while (!s->stop) {
        {
          std::lock_guard<std::mutex> lock(s->mutex);
          if (sent == output.size()) {
            output.clear(); sent = 0;
            if (admitted && !s->outgoing.empty()) {
              output = std::move(s->outgoing.front());
              s->outgoing.pop_front();
              sendProgress = Clock::now();
            } else if (admitted && s->host && Clock::now()-hosted>=std::chrono::milliseconds(500)) {
              std::vector<uint8_t> b;Put(b,s->host->Status());output=Frame("OLH4",b);hosted=Clock::now();sendProgress=hosted;
            } else if (admitted && Clock::now() - ranked >= std::chrono::milliseconds(200)) {
              std::vector<uint8_t> b;
              Put(b, s->standing.racing ? s->standing.rank : 0);
              Put(b, s->standing.racing ? 1 : 0);
              output = Frame("OLR1", b);
              ranked = Clock::now();
              sendProgress = ranked;
            } else if (admitted && Clock::now() - pinged > std::chrono::seconds(10)) {
              output = Frame("OLP1");
              pinged = Clock::now();
              sendProgress = Clock::now();
            }
          }
        }
        fd_set read, write; FD_ZERO(&read); FD_ZERO(&write); FD_SET(fd, &read);
        if (sent < output.size()) FD_SET(fd, &write);
        timeval tv{0, 100000};
        int ready = select(int(fd + 1), &read, &write, nullptr, &tv);
        if (ready < 0) { if (WouldBlock()) continue; throw std::runtime_error("Chat socket error"); }
        if (FD_ISSET(fd, &write)) {
#ifdef MSG_NOSIGNAL
          constexpr int flags = MSG_NOSIGNAL;
#else
          constexpr int flags = 0;
#endif
          int n = send(fd, reinterpret_cast<const char*>(output.data() + sent), int(output.size() - sent), flags);
          if (n > 0) { sent += size_t(n); sendProgress = Clock::now(); }
          else if (n == 0 || !WouldBlock()) throw std::runtime_error("Chat send failed");
        }
        if (FD_ISSET(fd, &read)) {
          uint8_t bytes[2048];
          int n = recv(fd, reinterpret_cast<char*>(bytes), sizeof(bytes), 0);
          if (n == 0) throw std::runtime_error("Chat disconnected");
          if (n < 0) { if (!WouldBlock()) throw std::runtime_error("Chat receive failed"); }
          else { input.insert(input.end(), bytes, bytes + n); received = Clock::now(); }
        }
        while (input.size() >= 8) {
          const uint32_t size = U32(input, 0);
          Require(size >= 8 && size <= 1032);
          if (input.size() < size) break;
          const std::string kind(input.begin() + 4, input.begin() + 8);
          const std::vector<uint8_t> b(input.begin() + 8, input.begin() + size);
          input.erase(input.begin(), input.begin() + size);
          if (kind == "HEL1") { Require(!greeted); greeted = true; continue; }
          Require(greeted);
          if (kind == "RLC2") {
            Require(!admitted && b.size() == 108);
            admitted = true;
            attempt = 0; // reset retry counter once admitted
            std::lock_guard<std::mutex> l(s->mutex);
            s->view.room = U32(b, 0);
            s->view.car = U32(b, 4);
            s->view.count = U32(b, 8);
            s->view.player = ReadChatString(b, 12, 32);
            s->view.title = String(b, 44, 64);
            s->view.connecting = false;
            s->view.connected = true;
            s->view.status = "Connected";
            s->view.error.clear();
          } else if (kind == "OLB1") {
            Require(admitted && b.size() >= 4);
            const uint32_t count = U32(b, 0);
            Require(count > 0 && count <= 16 && b.size() == 4 + count * 40);
            std::vector<StandingRow> rows;
            uint32_t seen = 0;
            for (uint32_t i = 0; i < count; ++i) {
              const size_t at = 4 + i * 40;
              StandingRow row;
              row.car = U32(b, at); row.rank = U32(b, at + 4);
              Require(row.car > 0 && row.car <= count && row.rank <= 40);
              Require(!(seen & (1u << (row.car - 1))));
              seen |= 1u << (row.car - 1);
              row.name = ReadChatString(b, at + 8, 32);
              rows.push_back(std::move(row));
            }
            SortStandings(rows);
            std::lock_guard<std::mutex> l(s->mutex);
            Require(count == s->view.count);
            s->view.standings = std::move(rows);
            s->view.standingsTimeMs = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());
          } else if (kind == "OLM1") {
            Require(admitted && b.size() == 204);
            ChatMessage msg;
            msg.seq = U32(b, 0); msg.scope = U32(b, 4); msg.car = U32(b, 8);
            msg.sender = ReadChatString(b, 12, 32); msg.text = ReadChatString(b, 44, 160);
            if (!msg.text.empty()) {
              std::lock_guard<std::mutex> l(s->mutex);
              AppendChatUnique(s->view.messages, std::move(msg), 100);
            }
          } else if (kind == "OLK1") {
            Require(admitted && b.empty());
          } else if (kind == "RLE1") {
            std::lock_guard<std::mutex> l(s->mutex);
            s->view.connecting = false;
            s->view.connected = false;
            if(s->host)s->host->Stop();
            s->view.closed = true;
            s->view.status = "Room closed";
            return;
          } else {
            throw std::runtime_error("Relay does not support Online chat; update relay.py/online_lobby.py");
          }
        }
        if (input.size() > 8192) throw std::runtime_error("Oversized chat response");
        if (Clock::now() - received > std::chrono::seconds(admitted ? 35 : 8)) throw std::runtime_error("Chat response timed out");
        if (sent < output.size() && Clock::now() - sendProgress > std::chrono::seconds(8)) throw std::runtime_error("Chat send timed out");
      }
      return;
    } catch (const std::exception& e) {
      if (s->stop) return;
      std::lock_guard<std::mutex> l(s->mutex);
      s->view.connecting = false;
      s->view.connected = false;
      s->view.error = e.what();
      s->view.status = "Disconnected";
    }
  }
}
}
