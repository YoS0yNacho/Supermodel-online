// Best-effort directory advertisement only. Failure never closes a hosted room.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_ONLINE_ROOM_DIRECTORY_H
#define INCLUDED_ONLINE_ROOM_DIRECTORY_H
#include "PlayerRoom.h"
namespace Online {
class RoomDirectory {
 std::atomic<bool> stop{false};std::thread worker;
public:
 RoomDirectory(PlayerRoom::Settings settings,const std::atomic<unsigned>& count,const std::atomic<unsigned>& state):worker([this,settings,&count,&state]{
#ifdef _WIN32
  WSADATA data;if(WSAStartup(MAKEWORD(2,2),&data))return;struct Cleanup{~Cleanup(){WSACleanup();}}cleanup;
#endif
  while(!stop){
   if(count){
    RelayUpnp::Owner connection;connection.fd=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    auto fd=connection.fd;
#ifndef _WIN32
    if(fd!=RelayUpnp::Bad&&fd>=FD_SETSIZE){RelayUpnp::Close(fd);connection.fd=fd=RelayUpnp::Bad;}
#endif
    if(fd!=RelayUpnp::Bad&&RelayUpnp::Nonblock(fd)){
     auto end=RelayUpnp::Clock::now()+std::chrono::seconds(1);auto address=settings.directory;
     int n=connect(fd,reinterpret_cast<sockaddr*>(&address),sizeof(address));
     bool good=n==0||(RelayUpnp::Again()&&RelayUpnp::Ready(fd,true,end,stop));int error=0;
#ifdef _WIN32
     int length=sizeof(error);
#else
     socklen_t length=sizeof(error);
#endif
     good=good&&!getsockopt(fd,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&error),&length)&&!error;
     PlayerRoom::Bytes b;PlayerRoom::Put(b,settings.id);b.insert(b.end(),settings.owner.begin(),settings.owner.end());PlayerRoom::Put(b,state);PlayerRoom::Put(b,count);
     auto output=PlayerRoom::Frame("HEL1");auto payload=PlayerRoom::Frame("ODA1",b);output.insert(output.end(),payload.begin(),payload.end());size_t sent=0;
     while(good&&sent<output.size()&&RelayUpnp::Ready(fd,true,end,stop)){
#ifdef MSG_NOSIGNAL
      constexpr int flags=MSG_NOSIGNAL;
#else
      constexpr int flags=0;
#endif
      n=send(fd,reinterpret_cast<const char*>(output.data()+sent),int(output.size()-sent),flags);if(n>0)sent+=size_t(n);else if(n==0||!RelayUpnp::Again())good=false;
     }
     // Await ack briefly so close cannot reset an unread response. No authority,
     // tickets, names, chat, standings or gameplay go to the directory.
     if(good&&sent==output.size()&&RelayUpnp::Ready(fd,false,end,stop)){uint8_t ack[16];(void)recv(fd,reinterpret_cast<char*>(ack),sizeof(ack),0);}
    }
   }
   for(unsigned i=0;i<20&&!stop;++i)std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
 }){}
 ~RoomDirectory(){stop=true;if(worker.joinable())worker.join();}
 RoomDirectory(const RoomDirectory&)=delete;RoomDirectory& operator=(const RoomDirectory&)=delete;
};
}
#endif
