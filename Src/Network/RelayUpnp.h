// Bounded, verified UPnP IGD mappings. Paired TCP/UDP uses ONE discovery/gateway.
// Only numeric private/local IPv4 gateway URLs; no redirects, DNS, DTD or entities.
// Short leases, renewal, ownership checks and best-effort deletion on normal close.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INCLUDED_RELAY_UPNP_H
#define INCLUDED_RELAY_UPNP_H
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#endif
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>
namespace RelayUpnp {
using Clock=std::chrono::steady_clock;
#ifdef _WIN32
using Socket=SOCKET;constexpr Socket Bad=INVALID_SOCKET;
inline void Close(Socket s){closesocket(s);}
inline bool Again(){int e=WSAGetLastError();return e==WSAEWOULDBLOCK||e==WSAEINPROGRESS||e==WSAEINTR;}
inline bool Nonblock(Socket s){u_long n=1;return ioctlsocket(s,FIONBIO,&n)==0;}
#else
using Socket=int;constexpr Socket Bad=-1;
inline void Close(Socket s){close(s);}
inline bool Again(){return errno==EAGAIN||errno==EWOULDBLOCK||errno==EINPROGRESS||errno==EINTR;}
inline bool Nonblock(Socket s){
#ifdef SO_NOSIGPIPE
 int yes=1;setsockopt(s,SOL_SOCKET,SO_NOSIGPIPE,&yes,sizeof(yes));
#endif
 return fcntl(s,F_SETFL,O_NONBLOCK)==0;
}
#endif
struct Owner { Socket fd=Bad;~Owner(){if(fd!=Bad)Close(fd);} };
inline std::string Lower(std::string x){for(auto& c:x)c=char(std::tolower(static_cast<unsigned char>(c)));return x;}
inline bool Local(uint32_t raw){uint32_t a=ntohl(raw);return (a>>24)==10 || (a>>20)==0xac1 || (a>>16)==0xc0a8 || (a>>24)==127;}
struct Url { std::string ip,path;uint16_t port=0;uint32_t raw=0; };
inline bool ParseUrl(const std::string& value,Url& out,uint32_t expected=0){
 if(value.size()>2048 || value.compare(0,7,"http://"))return false;
 for(unsigned char c:value)if(c<=32 || c==127 || c=='\"' || c=='<' || c=='>' || c=='\\' || c=='&' || c=='#')return false;
 auto slash=value.find('/',7);auto authority=value.substr(7,slash==std::string::npos?std::string::npos:slash-7);
 auto colon=authority.find(':');std::string ip=authority.substr(0,colon);in_addr addr{};
 if(inet_pton(AF_INET,ip.c_str(),&addr)!=1 || !Local(addr.s_addr) || (expected && expected!=addr.s_addr))return false;
 unsigned port=80;if(colon!=std::string::npos){auto text=authority.substr(colon+1);if(text.empty()||text.size()>5)return false;port=0;for(char c:text){if(c<'0'||c>'9')return false;port=port*10+unsigned(c-'0');}if(!port||port>65535)return false;}
 out={ip,slash==std::string::npos?"/":value.substr(slash),uint16_t(port),addr.s_addr};return true;
}
// Small bounded local-name reader, not a general XML/entity resolver. Accept
// namespace prefixes and comments; reject DTD/CDATA/entity declarations outright.
inline bool SafeXml(const std::string& xml){
 if(xml.size()>131072)return false;
 size_t at=0;while((at=xml.find("<!",at))!=std::string::npos){
  if(xml.compare(at,4,"<!--"))return false;
  auto end=xml.find("-->",at+4);if(end==std::string::npos)return false;at=end+3;
 }return true;
}
inline bool Element(const std::string& xml,const std::string& local,size_t& at,std::string& value){
 if(!SafeXml(xml))return false;
 while((at=xml.find('<',at))!=std::string::npos){
  if(xml.compare(at,4,"<!--")==0){at=xml.find("-->",at+4)+3;continue;}
  if(at+1>=xml.size())return false;
  if(xml[at+1]=='/'||xml[at+1]=='?'){++at;continue;}
  auto start=at+1,end=start;
  while(end<xml.size()&&(std::isalnum(static_cast<unsigned char>(xml[end]))||xml[end]==':'||xml[end]=='_'||xml[end]=='-'||xml[end]=='.'))++end;
  if(end==start){++at;continue;}
  const auto name=xml.substr(start,end-start);auto colon=name.rfind(':');const auto simple=colon==std::string::npos?name:name.substr(colon+1);
  char quote=0;size_t close=end;
  for(;close<xml.size();++close){char c=xml[close];if(quote){if(c==quote)quote=0;}else if(c=='\"'||c=='\'')quote=c;else if(c=='>')break;}
  if(close==xml.size())return false;
  at=close+1;if(simple!=local||xml[close-1]=='/')continue;
  const auto closing="</"+name;size_t tail=at;
  while((tail=xml.find(closing,tail))!=std::string::npos){
   size_t finish=tail+closing.size();while(finish<xml.size()&&std::isspace(static_cast<unsigned char>(xml[finish])))++finish;
   if(finish<xml.size()&&xml[finish]=='>'){value=xml.substr(at,tail-at);at=finish+1;return true;}++tail;
  }return false;
 }return false;
}
inline std::string Tag(const std::string& xml,const std::string& tag){
 size_t at=0;std::string v;if(!Element(xml,tag,at,v))return {};
 while(!v.empty()&&std::isspace(static_cast<unsigned char>(v.front())))v.erase(v.begin());
 while(!v.empty()&&std::isspace(static_cast<unsigned char>(v.back())))v.pop_back();
 if(v.find_first_of("<&>")!=std::string::npos)return {};
 return v;
}
inline bool Ready(Socket fd,bool writing,Clock::time_point end,const std::atomic<bool>& stop){
 while(!stop && Clock::now()<end){fd_set f;FD_ZERO(&f);FD_SET(fd,&f);timeval tv{0,100000};int n=select(int(fd+1),writing?nullptr:&f,writing?&f:nullptr,nullptr,&tv);if(n>0)return true;if(n<0&&!Again())return false;}return false;
}
struct Reply { int status=0;std::string body,local; };
inline Reply Http(const Url& url,const std::string& method,const std::string& action,const std::string& body,const std::atomic<bool>& stop,Clock::time_point deadline=Clock::time_point::max()){
 Reply reply;Owner owner;owner.fd=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(owner.fd==Bad||!Nonblock(owner.fd))return reply;
#ifndef _WIN32
 if(owner.fd>=FD_SETSIZE)return reply;
#endif
 sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_port=htons(url.port);addr.sin_addr.s_addr=url.raw;
 auto end=std::min(Clock::now()+std::chrono::seconds(2),deadline);int n=connect(owner.fd,reinterpret_cast<sockaddr*>(&addr),sizeof(addr));
 if(n && (!Again() || !Ready(owner.fd,true,end,stop)))return reply;
 int error=0;
#ifdef _WIN32
 int size=sizeof(error);
#else
 socklen_t size=sizeof(error);
#endif
 if(getsockopt(owner.fd,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&error),&size)||error)return reply;
 sockaddr_in local{};
#ifdef _WIN32
 int localSize=sizeof(local);
#else
 socklen_t localSize=sizeof(local);
#endif
 if(!getsockname(owner.fd,reinterpret_cast<sockaddr*>(&local),&localSize)){char text[INET_ADDRSTRLEN]{};inet_ntop(AF_INET,&local.sin_addr,text,sizeof(text));reply.local=text;}
 std::string request=method+" "+url.path+" HTTP/1.1\r\nHost: "+url.ip+":"+std::to_string(url.port)+"\r\nConnection: close\r\n";
 if(!action.empty())request+="Content-Type: text/xml; charset=\"utf-8\"\r\nSOAPAction: \""+action+"\"\r\n";
 request+="Content-Length: "+std::to_string(body.size())+"\r\n\r\n"+body;
 size_t sent=0;while(sent<request.size() && Ready(owner.fd,true,end,stop)){
#ifdef MSG_NOSIGNAL
 constexpr int flags=MSG_NOSIGNAL;
#else
 constexpr int flags=0;
#endif
 n=send(owner.fd,request.data()+sent,int(request.size()-sent),flags);if(n>0)sent+=size_t(n);else if(n==0||!Again())return reply;
 }if(sent!=request.size())return reply;
 std::string input;bool complete=false;size_t content=std::string::npos;
 while(input.size()<131072 && Ready(owner.fd,false,end,stop)){
  char b[4096];n=recv(owner.fd,b,sizeof(b),0);if(n==0){complete=true;break;}if(n<0){if(Again())continue;return reply;}input.append(b,size_t(n));
  auto h=input.find("\r\n\r\n");if(h!=std::string::npos){auto head=Lower(input.substr(0,h));auto c=head.find("\r\ncontent-length:");if(c!=std::string::npos){c+=17;while(c<head.size()&&head[c]==' ')++c;size_t len=0;bool digits=false;for(;c<head.size()&&head[c]>='0'&&head[c]<='9';++c){digits=true;len=len*10+size_t(head[c]-'0');if(len>131072)return reply;}if(digits)content=h+4+len;}if(content!=std::string::npos&&input.size()>=content){complete=true;break;}}
 }
 if(!complete || input.size()>=131072)return reply;
 auto h=input.find("\r\n\r\n");if(h==std::string::npos||h>16384||input.size()<12)return reply;
 if(input.compare(0,7,"HTTP/1.") || input[8]!=' ')return reply;
 int status=0;for(size_t i=9;i<12;++i){if(input[i]<'0'||input[i]>'9')return reply;status=status*10+input[i]-'0';}
 auto data=input.substr(h+4);auto head=Lower(input.substr(0,h));
 if(head.find("transfer-encoding: chunked")!=std::string::npos){
  std::string decoded;size_t at=0;while(true){auto line=data.find("\r\n",at);if(line==std::string::npos||line-at>8)return reply;size_t len=0;for(size_t i=at;i<line;++i){char c=char(std::tolower(static_cast<unsigned char>(data[i])));int digit=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;if(digit<0)return reply;len=len*16+size_t(digit);if(len>131072)return reply;}at=line+2;if(!len)break;if(len>data.size()-at||data.size()-at-len<2||data.compare(at+len,2,"\r\n"))return reply;decoded.append(data,at,len);at+=len+2;}data=std::move(decoded);
 }else if(content!=std::string::npos)data.resize(content-(h+4));
 reply.status=status;reply.body=std::move(data);return reply;
}
struct Gateway { Url control;std::string service,local; };
inline std::vector<Gateway> Descriptions(const Url& location,const Reply& reply){
 std::vector<Gateway> result;if(reply.status!=200||reply.local.empty()||!SafeXml(reply.body))return result;
 std::string base="http://"+location.ip+":"+std::to_string(location.port)+location.path;
 auto explicitBase=Tag(reply.body,"URLBase");if(!explicitBase.empty())base=explicitBase;
 size_t at=0;std::string block;unsigned budget=32;
 while(budget--&&result.size()<8&&Element(reply.body,"service",at,block)){
  auto type=Tag(block,"serviceType");if(type!="urn:schemas-upnp-org:service:WANIPConnection:1"&&type!="urn:schemas-upnp-org:service:WANIPConnection:2"&&type!="urn:schemas-upnp-org:service:WANPPPConnection:1")continue;
  auto path=Tag(block,"controlURL");if(path.empty())continue;
  if(path.compare(0,7,"http://")){Url baseUrl;if(!ParseUrl(base,baseUrl,location.raw))continue;if(path[0]=='/')path="http://"+baseUrl.ip+":"+std::to_string(baseUrl.port)+path;else path="http://"+baseUrl.ip+":"+std::to_string(baseUrl.port)+baseUrl.path.substr(0,baseUrl.path.rfind('/')+1)+path;}
  Url control;if(!ParseUrl(path,control,location.raw))continue;result.push_back({control,type,reply.local});
 }return result;
}
inline bool Description(const Url& location,const Reply& reply,Gateway& gateway){auto all=Descriptions(location,reply);if(all.empty())return false;gateway=all.front();return true;}
inline Reply Soap(const Gateway& gateway,const std::string& method,const std::string& args,const std::atomic<bool>& stop,Clock::time_point deadline=Clock::time_point::max()){
 std::string body="<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:"+method+" xmlns:u=\""+gateway.service+"\">"+args+"</u:"+method+"></s:Body></s:Envelope>";
 return Http(gateway.control,"POST",gateway.service+"#"+method,body,stop,deadline);
}
inline std::string Key(uint16_t port,const std::string& protocol="UDP"){return "<NewRemoteHost></NewRemoteHost><NewExternalPort>"+std::to_string(port)+"</NewExternalPort><NewProtocol>"+protocol+"</NewProtocol>";}
inline bool ValidProtocol(const std::string& protocol){return protocol=="TCP"||protocol=="UDP";}
inline bool Destination(const Reply& r,const Gateway& g,uint16_t port){return r.status==200&&Tag(r.body,"NewInternalClient")==g.local&&Tag(r.body,"NewInternalPort")==std::to_string(port);}
inline bool Enabled(const Reply& r){auto enabled=Lower(Tag(r.body,"NewEnabled"));return enabled=="1"||enabled=="true";}
inline bool Ours(const Reply& r,const Gateway& g,uint16_t port,const std::string& label){return Destination(r,g,port)&&Tag(r.body,"NewPortMappingDescription")==label;}
inline std::string LogValue(std::string s){for(auto& c:s)if(static_cast<unsigned char>(c)<32||static_cast<unsigned char>(c)>126)c='?';if(s.size()>120)s.resize(120);return s;}
inline void Diagnostic(const char* operation,const std::string& protocol,uint16_t port,const Reply& r){
 auto code=LogValue(Tag(r.body,"errorCode")),reason=LogValue(Tag(r.body,"errorDescription"));
 std::fprintf(stderr,"Self-host UPnP: %s %u %s: HTTP %d, IGD %s%s%s.\n",protocol.c_str(),unsigned(port),operation,r.status,code.empty()?"unknown":code.c_str(),reason.empty()?"":" (",reason.empty()?"":(reason+")").c_str());
 if(code=="725")std::fprintf(stderr,"Self-host UPnP: router only accepts permanent leases; no permanent mapping was requested. Use manual forwarding or central hosting.\n");
}
inline bool Add(const Gateway& g,uint16_t port,const std::string& label,const std::atomic<bool>& stop,const std::string& protocol="UDP",Clock::time_point deadline=Clock::time_point::max()){
 if(!ValidProtocol(protocol)||stop||Clock::now()>=deadline)return false;
 auto r=Soap(g,"AddPortMapping",Key(port,protocol)+"<NewInternalPort>"+std::to_string(port)+"</NewInternalPort><NewInternalClient>"+g.local+"</NewInternalClient><NewEnabled>1</NewEnabled><NewPortMappingDescription>"+label+"</NewPortMappingDescription><NewLeaseDuration>120</NewLeaseDuration>",stop,deadline);
 if(r.status!=200){Diagnostic("AddPortMapping failed",protocol,port,r);return false;}return true;
}
inline bool Release(const Gateway& g,uint16_t port,const std::string& label,const std::atomic<bool>& stop,const std::string& protocol="UDP",Clock::time_point deadline=Clock::time_point::max()){
 return ValidProtocol(protocol)&&Ours(Soap(g,"GetSpecificPortMappingEntry",Key(port,protocol),stop,deadline),g,port,label)&&Soap(g,"DeletePortMapping",Key(port,protocol),stop,deadline).status==200;
}
inline bool Acquire(const Gateway& g,uint16_t port,const std::string& label,const std::atomic<bool>& stop,const std::string& protocol="UDP",Clock::time_point deadline=Clock::time_point::max()){
 if(!ValidProtocol(protocol)||stop||Clock::now()>=deadline)return false;
 auto previous=Soap(g,"GetSpecificPortMappingEntry",Key(port,protocol),stop,deadline);
 if(previous.status!=500||Tag(previous.body,"errorCode")!="714"){
  if(previous.status==200)std::fprintf(stderr,"Self-host UPnP: %s %u already has a mapping; not overwriting it.\n",protocol.c_str(),unsigned(port));
  else Diagnostic("existing-mapping query failed",protocol,port,previous);
  return false;
 }
 // An interrupted Add may have reached the router even without a reply.
 // On cancellation/deadline, independently query and remove ONLY our exact
 // rule, with a separate two-second cleanup budget (never a permanent lease).
 auto cleanup=[&]{std::atomic<bool> cleanupStop{false};Release(g,port,label,cleanupStop,protocol,Clock::now()+std::chrono::seconds(2));};
 if(!Add(g,port,label,stop,protocol,deadline)){if(stop||Clock::now()>=deadline)cleanup();return false;}
 auto check=Soap(g,"GetSpecificPortMappingEntry",Key(port,protocol),stop,deadline);
 if(Ours(check,g,port,label)&&Enabled(check)){
  std::fprintf(stderr,"Self-host UPnP: %s external %u -> %s:%u enabled and read-back verified (120-second lease requested).\n",protocol.c_str(),unsigned(port),g.local.c_str(),unsigned(port));return true;
 }
 std::fprintf(stderr,"Self-host UPnP: %s %u Add succeeded but read-back FAILED: target=%s:%s enabled=%s; expected %s:%u and this session's description.\n",protocol.c_str(),unsigned(port),LogValue(Tag(check.body,"NewInternalClient")).c_str(),LogValue(Tag(check.body,"NewInternalPort")).c_str(),LogValue(Tag(check.body,"NewEnabled")).c_str(),g.local.c_str(),unsigned(port));
 if(Ours(check,g,port,label)||stop||Clock::now()>=deadline)cleanup(); // Only our exact rule.
 return false;
}
inline bool Renew(const Gateway& g,uint16_t port,const std::string& label,const std::atomic<bool>& stop,const std::string& protocol="UDP"){
 if(!ValidProtocol(protocol)||!Ours(Soap(g,"GetSpecificPortMappingEntry",Key(port,protocol),stop),g,port,label)||!Add(g,port,label,stop,protocol))return false;
 auto check=Soap(g,"GetSpecificPortMappingEntry",Key(port,protocol),stop);return Ours(check,g,port,label)&&Enabled(check);
}
inline std::vector<Url> Discover(const std::atomic<bool>& stop){
 std::vector<Url> urls;Owner owner;owner.fd=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);if(owner.fd==Bad||!Nonblock(owner.fd))return urls;
#ifndef _WIN32
 if(owner.fd>=FD_SETSIZE)return urls;
#endif
 sockaddr_in multicast{};multicast.sin_family=AF_INET;multicast.sin_port=htons(1900);inet_pton(AF_INET,"239.255.255.250",&multicast.sin_addr);
 const char message[]="M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 1\r\nST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n\r\n";
 sendto(owner.fd,message,sizeof(message)-1,0,reinterpret_cast<sockaddr*>(&multicast),sizeof(multicast));auto end=Clock::now()+std::chrono::seconds(2);
 unsigned budget=64;while(budget-- && urls.size()<4&&Ready(owner.fd,false,end,stop)){
  char buffer[4097];sockaddr_in from{};
#ifdef _WIN32
  int len=sizeof(from);
#else
  socklen_t len=sizeof(from);
#endif
  int n=recvfrom(owner.fd,buffer,4096,0,reinterpret_cast<sockaddr*>(&from),&len);if(n<=0||n>=4096)continue;
  std::string data(buffer,size_t(n));auto lower=Lower(data);if(lower.compare(0,12,"http/1.1 200"))continue;
  auto at=lower.find("\r\nlocation:");if(at==std::string::npos)continue;at+=11;while(at<data.size()&&data[at]==' ')++at;auto finish=data.find("\r\n",at);if(finish==std::string::npos)continue;
  Url url;if(ParseUrl(data.substr(at,finish-at),url,from.sin_addr.s_addr)){
   bool duplicate=false;for(auto& old:urls)duplicate|=old.raw==url.raw&&old.port==url.port&&old.path==url.path;if(!duplicate)urls.push_back(url);
  }
 }return urls;
}
class Mapping {
 std::atomic<bool> stop{false},complete{false};std::atomic<unsigned> ready{0};std::thread worker;
public:
 using Discovery=std::function<std::vector<Url>(const std::atomic<bool>&)>;
 // BOTH deliberately uses one SSDP discovery and one selected gateway for TCP
 // and UDP, not two competing discovery workers. The optional factory permits
 // deterministic mock-router tests without changing production network routes.
 Mapping(uint16_t port,std::string label,std::string protocol="UDP",Discovery discovery=Discover):worker([this,port,label,protocol,discovery]{
#ifdef _WIN32
  WSADATA data;if(WSAStartup(MAKEWORD(2,2),&data)){complete=true;return;}struct Cleanup{~Cleanup(){WSACleanup();}}cleanup;
#endif
  if(protocol!="BOTH"&&!ValidProtocol(protocol)){complete=true;return;}
  const unsigned wanted=protocol=="BOTH"?3u:protocol=="TCP"?1u:2u;unsigned owned=0;Gateway chosen;
  auto end=Clock::now()+std::chrono::seconds(18);
  auto locations=discovery(stop);
  for(auto& location:locations){
   if(stop||Clock::now()>=end)break;
   auto response=Http(location,"GET","","",stop,end);
   auto gateways=Descriptions(location,response);
   for(auto& gateway:gateways){
    if(stop||Clock::now()>=end)break;
    auto status=Soap(gateway,"GetStatusInfo","",stop,end);auto state=Tag(status.body,"NewConnectionStatus");
    if(!state.empty()&&state!="Connected"){
     std::fprintf(stderr,"Self-host UPnP: skipping %s on %s:%u (WAN status %s).\n",gateway.service.c_str(),gateway.control.ip.c_str(),unsigned(gateway.control.port),LogValue(state).c_str());continue;
    }
    std::fprintf(stderr,"Self-host UPnP: shared gateway %s:%u, LAN target %s, service %s%s.\n",gateway.control.ip.c_str(),unsigned(gateway.control.port),gateway.local.c_str(),gateway.service.c_str(),state.empty()?" (WAN status unavailable)":" (Connected)");
    unsigned mapped=0;
    for(unsigned bit:{1u,2u})if(wanted&bit){
     if(Acquire(gateway,port,label,stop,bit==1?"TCP":"UDP",end))mapped|=bit;
    }
    if(mapped){owned=mapped;chosen=gateway;ready=mapped;break;}
   }
   if(owned)break;
  }
  complete=true;
  std::fprintf(stderr,"Self-host UPnP: setup complete; TCP=%s UDP=%s. Central TCP+UDP reachability test is still authoritative.\n",!(wanted&1)?"not requested":(ready.load()&1)?"verified":"not verified",!(wanted&2)?"not requested":(ready.load()&2)?"verified":"not verified");
  if(!owned)return;
  auto renew=Clock::now()+std::chrono::seconds(60);
  while(!stop){std::this_thread::sleep_for(std::chrono::milliseconds(100));if(Clock::now()<renew)continue;
   for(unsigned bit:{1u,2u})if(owned&bit){
    if(!Renew(chosen,port,label,stop,bit==1?"TCP":"UDP")){
     ready.fetch_and(~bit);std::fprintf(stderr,"Self-host UPnP: %s %u renewal/read-back failed; no longer verified.\n",bit==1?"TCP":"UDP",unsigned(port));
    }else ready.fetch_or(bit);
   }renew=Clock::now()+std::chrono::seconds(60);
  }
  // Cleanup each protocol separately. Never delete a pre-existing/changed rule.
  std::atomic<bool> cleanupStop{false};
  for(unsigned bit:{1u,2u})if(owned&bit)Release(chosen,port,label,cleanupStop,bit==1?"TCP":"UDP");
 }){}
 bool Complete()const{return complete;}
 unsigned ReadyMask()const{return ready;}
 ~Mapping(){stop=true;if(worker.joinable())worker.join();}
 Mapping(const Mapping&)=delete;Mapping& operator=(const Mapping&)=delete;
};
}
#endif
