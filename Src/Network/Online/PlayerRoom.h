// Native single-room authority and chat; transport-independent bounded state machine.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_ONLINE_PLAYER_ROOM_H
#define INCLUDED_ONLINE_PLAYER_ROOM_H
#include "Network/RelayUpnp.h"
#include "RoomSettings.h"
#include "Network/Games/Srally2/ResearchBuild.h"
#include <array>
#include <cstring>
#include <functional>
#include <map>
#include <random>
namespace Online {
class PlayerRoom {
public:
 using Bytes=std::vector<uint8_t>;using Token=std::array<uint8_t,16>;
 struct Member {Token ticket{},cookie{};};
 struct Settings {uint32_t id=0,game=0,tick=20,ip=0;uint16_t port=0;std::string title;Token owner{};sockaddr_in directory{};};
 using Send=std::function<bool(uint32_t,const Bytes&)>;
 using Launch=std::function<bool(unsigned,unsigned,const std::vector<Member>&)>;
 std::atomic<unsigned> listedCount{0},listedState{0};
 explicit PlayerRoom(Settings settings,Send send,Launch launch):settings(std::move(settings)),send(std::move(send)),launch(std::move(launch)){}
 static uint32_t Read(const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
 static void Put(Bytes& b,uint32_t n){for(unsigned i=0;i<4;++i)b.push_back(uint8_t(n>>(8*i)));}
 static void Text(Bytes& b,const std::string& s,size_t n){auto start=b.size();b.resize(start+n);std::memcpy(b.data()+start,s.data(),std::min(s.size(),n-1));}
 static Bytes Frame(const char* id,const Bytes& body={}){Bytes b;Put(b,uint32_t(body.size()+8));b.insert(b.end(),id,id+4);b.insert(b.end(),body.begin(),body.end());return b;}
 static std::string Name(const uint8_t* p,size_t n){std::string s;for(size_t i=0;i<n&&p[i];++i)s+=p[i]>=32&&p[i]<127?char(p[i]):'?';return s.empty()?"Player":s;}
 static std::string Chat(const uint8_t* p,size_t n){
  std::string s;for(size_t i=0;i<n&&p[i]&&s.size()<159;){unsigned c=p[i],k=c<128?1:c>=0xc2&&c<=0xdf?2:c>=0xe0&&c<=0xef?3:c>=0xf0&&c<=0xf4?4:0;
   if(c<32||c==127){if(c==9||c==10||c==13)s+=' ';++i;continue;}
   bool good=k&&i+k<=n&&s.size()+k<=159;
   for(unsigned j=1;good&&j<k;++j)good=(p[i+j]&0xc0)==0x80;
   if(good&&k==3)good=!(c==0xe0&&p[i+1]<0xa0)&&!(c==0xed&&p[i+1]>=0xa0);
   if(good&&k==4)good=!(c==0xf0&&p[i+1]<0x90)&&!(c==0xf4&&p[i+1]>=0x90);
   if(!good){s+='?';++i;}else{s.append(reinterpret_cast<const char*>(p+i),k);i+=k;}
  }while(!s.empty()&&s.back()==' ')s.pop_back();while(!s.empty()&&s.front()==' ')s.erase(s.begin());return s;
 }
 bool Registered(uint32_t id)const{return clients.count(id)!=0;}
 bool Closed()const{return closed;}
 bool Message(uint32_t id,uint32_t ip,const Bytes& packet){
  const std::string kind(packet.begin()+4,packet.begin()+8);const uint8_t* b=packet.data()+8;size_t n=packet.size()-8;
  auto it=clients.find(id);
  if(it==clients.end()){
   Client c;c.id=id;
   if(kind==Srally2Research::LobbyHello&&n==60&&Read(b)==1&&(Read(b+4)&~127u)==0&&(Read(b+8)&1)&&!(Read(b+8)&~255u)/*AsyncNet + capability bits 2,4,16,32,64,128*/&&state==0&&!closed){
    bool owner=std::memcmp(b+44,settings.owner.data(),16)==0;
    if(!(Read(b+4)&(1u<<settings.game))||(!owner&&(!order.size()||!Zero(b+44)))||(owner&&ownerId)||order.size()>=Capacity())return false;
    if(owner&&ntohl(ip)!=0x7f000001)return false;
    c.optionsCapable=(Read(b+8)&2)!=0;c.nativeCapable=(Read(b+8)&4)!=0;c.hornetCapable=(Read(b+8)&16)!=0;c.raceCapable=(Read(b+8)&32)!=0;c.pingCapable=(Read(b+8)&128)!=0;
    if(owner && c.nativeCapable)options=NativeRoomSettings::Defaults(settings.game);
    if((options&NativeRoomSettings::Enabled)&&!c.nativeCapable){Error(id,"Update this client: native room settings are enabled.");return false;}
    if(NativeRoomSettings::CarLimit(settings.game)){for(unsigned car=1;car<=NativeRoomSettings::CarLimit(settings.game);++car){bool used=false;for(auto member:order)used|=clients.at(member).nativeCar==car;if(!used){c.nativeCar=car;break;}}}
    if(NativeRoomSettings::HasHornet(settings.game,options)&&!c.hornetCapable){Error(id,"Update this client: Hornet is enabled in this room.");return false;}
    if(NativeRoomSettings::HasRealPlayers(settings.game,options)&&!c.raceCapable){Error(id,"Update this client: Real players only is enabled in this room.");return false;}
    if(NativeRoomSettings::HasNoCollisions(settings.game,options)&&!c.raceCapable){Error(id,"Update this client: collisions are disabled in this room.");return false;}
    if(options && !c.optionsCapable){Error(id,"Update this client: Secret Cars is enabled in this room.");return false;}
    c.name=Name(b+12,32);if(owner)ownerId=id;c.number=nextMember++;c.role=1;order.push_back(id);clients.emplace(id,c);
    send(id,Frame("HEL1"));Broadcast();for(auto& m:history)send(id,m);
    if(owner)Post(0,"System","Self-host ready: room, chat, standings, launch and TCP/UDP gameplay use the creator. Central only lists this room.");
    return true;
   }
   if(kind=="RLC1"&&n==52&&Read(b)==1&&state>0&&state<3&&!closed){
    for(size_t i=0;i<frozen.size();++i)if(!std::memcmp(b+4,frozen[i].auth.ticket.data(),16)){
     c.role=2;c.car=uint32_t(i+1);c.name=frozen[i].name;
     for(auto& old:clients)if(old.second.role==2&&old.second.car==c.car){old.second.role=3;send(old.first,Frame("RLE1"));}
     clients.emplace(id,c);Bytes w;Put(w,settings.id);Put(w,c.car);Put(w,uint32_t(frozen.size()));Text(w,c.name,32);Text(w,settings.title,64);
     send(id,Frame("HEL1"));send(id,Frame("RLC2",w));for(auto& m:history)send(id,m);return true;
    }
   }return false;
  }
  auto& c=it->second;auto now=Now();if(now-c.window>=1000000){c.window=now;c.requests=0;}if(++c.requests>24||closed)return false;
  if(c.role==1){
   if(kind=="OLQ1"&&!n){Snapshot(id);return true;}
   if(kind=="OLQ2"&&n==12&&c.pingCapable){c.pingMs=std::min<uint32_t>(Read(b+8),9999);Bytes e(b,b+8);send(id,Frame("OLP2",e));Snapshot(id);return true;}
   if(kind=="OLS1"&&n==160){Post(Car(id),c.name,Chat(b,n));return true;}
   if(kind=="OLL1"&&!n){Drop(id);return false;}
   if(kind=="ORO1"&&n==8){
    const uint32_t value=Read(b+4);
    if(Read(b)!=settings.id||id!=ownerId||state||!c.optionsCapable||!NativeRoomSettings::Valid(settings.game,value)||((value&NativeRoomSettings::Enabled)&&(!c.nativeCapable||!AllNative()))){
     Error(id,"Only the master can change valid room options before launch; update all clients for native settings.");return true;
    }
    if(value){for(auto member:order)if(!clients.at(member).optionsCapable){Error(id,"Update all room members before enabling room options.");return true;}}
    if(NativeRoomSettings::HasHornet(settings.game,value)){for(auto member:order)if(!clients.at(member).hornetCapable){Error(id,"Update all room members before enabling Hornet.");return true;}}
    if(NativeRoomSettings::HasRealPlayers(settings.game,value)){for(auto member:order)if(!clients.at(member).raceCapable){Error(id,"Update all room members before enabling Real players only.");return true;}}
    if(NativeRoomSettings::HasNoCollisions(settings.game,value)){for(auto member:order)if(!clients.at(member).raceCapable){Error(id,"Update all room members before disabling collisions.");return true;}}
    options=value;Broadcast();return true;
   }
   if(kind=="OCN1"&&n==8){
    const auto car=Read(b+4);
    if(Read(b)!=settings.id||state||!c.nativeCapable||!AllNative()||!NativeRoomSettings::CarLimit(settings.game)||car>NativeRoomSettings::CarLimit(settings.game)){
     Error(id,"CAR NUMBER can only be selected before launch with updated clients.");return true;}
    for(auto member:order)if(member!=id&&car&&clients.at(member).nativeCar==car){Error(id,"That CAR NUMBER is already selected by another player.");return true;}
    c.nativeCar=car;Broadcast();return true;
   }
   if(kind=="OLT1"&&n==4&&Read(b)==settings.id){
    if(id!=ownerId||state||order.size()<2){Error(id,"Only the master can start with at least two members.");return true;}
    if(AllNative()&&NativeRoomSettings::CarLimit(settings.game)){for(auto member:order)if(!clients.at(member).nativeCar){Error(id,"Every player must select a unique CAR NUMBER before Start Link.");return true;}}
    std::vector<Member> auth;std::vector<Token> issued;
    try{for(auto member:order){Frozen f;f.name=clients.at(member).name;f.number=clients.at(member).number;f.nativeCar=AllNative()?clients.at(member).nativeCar:0;do{f.auth.ticket=Random();}while(std::find(issued.begin(),issued.end(),f.auth.ticket)!=issued.end());issued.push_back(f.auth.ticket);do{f.auth.cookie=Random();}while(std::find(issued.begin(),issued.end(),f.auth.cookie)!=issued.end());issued.push_back(f.auth.cookie);frozen.push_back(f);auth.push_back(f.auth);}}
    catch(...){frozen.clear();Error(id,"Secure ticket generation unavailable. Recreate the room.");return true;}
    if(!launch(settings.game,settings.tick,auth)){frozen.clear();Error(id,"Native host could not prepare. Recreate the room.");return true;}
    frozenNative=AllNative();state=1;launchPending=true;deadline=now+90000000;Broadcast();return true;
   }
  }else if(c.role==2){
   if(kind=="OLP1"&&!n){send(id,Frame("OLK1"));return true;}
   if(kind=="OLS1"&&n==160){Post(c.car,c.name,Chat(b,n));return true;}
   if(kind=="OLH4"&&n==4&&c.car==1&&Read(b)>=1&&Read(b)<=3)return true; // Actual native state is authoritative.
   if(kind=="OLR1"&&n==8&&Read(b+4)<=1&&((Read(b+4)==1&&Read(b)>=1&&Read(b)<=40)||(Read(b+4)==0&&Read(b)==0))){
    frozen[c.car-1].rank=Read(b);frozen[c.car-1].rankTime=now;frozen[c.car-1].rankOwner=id;Bytes rows;Put(rows,uint32_t(frozen.size()));
    for(size_t i=0;i<frozen.size();++i){auto& f=frozen[i];Put(rows,uint32_t(i+1));Put(rows,now-f.rankTime<=2000000?f.rank:0);Text(rows,f.name,32);}send(id,Frame("OLB1",rows));return true;
   }
  }return false;
 }
 void Drop(uint32_t id){auto it=clients.find(id);if(it==clients.end())return;
  if(it->second.role==1&&state==0&&!closed){order.erase(std::remove(order.begin(),order.end(),id),order.end());if(id==ownerId){closed=true;listedState=3;}else Broadcast();}
  if(it->second.role==2&&it->second.car&&frozen[it->second.car-1].rankOwner==id){frozen[it->second.car-1].rank=0;frozen[it->second.car-1].rankTime=0;}
  clients.erase(it);listedCount=unsigned(order.size());
 }
 void Pump(bool armed,bool started,bool failed){
  if(closed)return;
  if(failed||(state==1&&Now()>deadline)){state=3;closed=true;listedState=3;for(auto& c:clients)send(c.first,Frame("RLE1"));return;}
  if(launchPending&&armed){launchPending=false;for(size_t i=0;i<order.size();++i){Bytes b;Put(b,settings.id);Put(b,settings.game);Put(b,uint32_t(i));Put(b,uint32_t(order.size()));b.insert(b.end(),frozen[i].auth.ticket.begin(),frozen[i].auth.ticket.end());const auto& member=clients.at(order[i]);if(member.optionsCapable)Put(b,options);if(member.nativeCapable)Put(b,frozen[i].nativeCar);send(order[i],Frame(member.nativeCapable?"OLG5":member.optionsCapable?"OLG3":"OLG1",b));}}
  if(started&&state==1){state=2;Broadcast();}listedState=state;listedCount=unsigned(order.size());
 }
private:
 struct Client {uint32_t id=0,number=0,car=0,nativeCar=0,pingMs=0;unsigned role=0,requests=0;bool optionsCapable=false,nativeCapable=false,hornetCapable=false,raceCapable=false,pingCapable=false;uint64_t window=Now();std::string name;};
 struct Frozen {Member auth;std::string name;uint32_t number=0,rank=0,rankOwner=0,nativeCar=0;uint64_t rankTime=0;};
 Settings settings;Send send;Launch launch;std::map<uint32_t,Client> clients;std::vector<uint32_t> order;std::vector<Frozen> frozen;std::vector<Bytes> history;
 uint32_t ownerId=0,nextMember=1,seq=1,state=0,options=0;bool closed=false,launchPending=false,frozenNative=false;uint64_t deadline=0;
 static uint64_t Now(){return uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(RelayUpnp::Clock::now().time_since_epoch()).count());}
 static bool Zero(const uint8_t* p){for(unsigned i=0;i<16;++i)if(p[i])return false;return true;}
 static Token Random(){std::random_device r;Token t{};do{for(unsigned i=0;i<16;i+=4){auto x=r();for(unsigned j=0;j<4;++j)t[i+j]=uint8_t(x>>(j*8));}}while(Zero(t.data()));return t;}
 bool AllNative()const{if(state)return frozenNative;for(auto member:order)if(!clients.at(member).nativeCapable)return false;return !order.empty();}
 unsigned Capacity()const{return settings.game<2?16:settings.game==4?Srally2Research::StockCapacity:settings.game==5?6:8;}
 uint32_t Car(uint32_t id)const{auto i=std::find(order.begin(),order.end(),id);return i==order.end()?0:uint32_t(i-order.begin()+1);}
 void Error(uint32_t id,const std::string& text){Bytes b;Text(b,text,160);send(id,Frame("OLE1",b));}
 void Snapshot(uint32_t id){auto it=clients.find(id);if(it==clients.end())return;Bytes b;Put(b,1);Put(b,it->second.number);Put(b,settings.id);send(id,Frame("OLB1",b));b.clear();
  Put(b,settings.id);Put(b,settings.game);Put(b,state);Put(b,uint32_t(order.size()));Put(b,state?frozen.at(0).number:clients.at(ownerId).number);Text(b,settings.title,64);
  if(state){for(auto& f:frozen){Put(b,f.number);Text(b,f.name,32);}}else for(auto member:order){Put(b,clients.at(member).number);Text(b,clients.at(member).name,32);}send(id,Frame("OLR1",b));b.clear();Put(b,settings.id);Put(b,settings.port);Put(b,3);Put(b,0);send(id,Frame("OLU2",b));if(it->second.optionsCapable){b.clear();Put(b,settings.id);Put(b,options);send(id,Frame("ORO1",b));}
  if(it->second.nativeCapable){b.clear();Put(b,settings.id);Put(b,AllNative()?1:0);Put(b,uint32_t(order.size()));
   for(size_t i=0;i<order.size();++i){Put(b,state?frozen[i].number:clients.at(order[i]).number);Put(b,AllNative()?(state?frozen[i].nativeCar:clients.at(order[i]).nativeCar):0);}send(id,Frame("ORN1",b));}
  if(it->second.pingCapable){b.clear();Put(b,settings.id);Put(b,uint32_t(order.size()));
   for(size_t i=0;i<order.size();++i){if(state){Put(b,frozen[i].number);Put(b,0);}else{Put(b,clients.at(order[i]).number);Put(b,clients.at(order[i]).pingMs);}}send(id,Frame("ORP1",b));}
  send(id,Frame("OLZ1"));
 }
 void Broadcast(){for(auto& c:clients)if(c.second.role==1)Snapshot(c.first);listedCount=unsigned(order.size());listedState=state;}
 void Post(uint32_t car,const std::string& sender,const std::string& text){if(text.empty())return;Bytes b;Put(b,seq++);Put(b,settings.id);Put(b,car);Text(b,sender,32);Text(b,text,160);auto packet=Frame("OLM1",b);history.push_back(packet);if(history.size()>64)history.erase(history.begin());for(auto& c:clients)if(c.second.role==1||c.second.role==2)send(c.first,packet);}
};
}
#endif
