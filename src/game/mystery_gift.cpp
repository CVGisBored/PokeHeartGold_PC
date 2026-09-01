#include "game/mystery_gift.hpp"
#include "assets/pokemon_data.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <map>
#include <random>
#include <sstream>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using HgSocket=SOCKET;
static constexpr HgSocket HG_INVALID_SOCKET=INVALID_SOCKET;
static void hg_close_socket(HgSocket s){if(s!=INVALID_SOCKET)closesocket(s);}
#else
#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
using HgSocket=int;
static constexpr HgSocket HG_INVALID_SOCKET=-1;
static void hg_close_socket(HgSocket s){if(s>=0)::close(s);}
#endif

namespace {
struct SocketInit {
    bool ok=true;
#ifdef _WIN32
    WSADATA data{};
    SocketInit(){ok=WSAStartup(MAKEWORD(2,2),&data)==0;}
    ~SocketInit(){if(ok)WSACleanup();}
#endif
};

static std::string trim(std::string v){
    while(!v.empty()&&(v.back()=='\r'||v.back()=='\n'||v.back()==' '||v.back()=='\t'))v.pop_back();
    std::size_t p=0;while(p<v.size()&&(v[p]==' '||v[p]=='\t'))++p;return v.substr(p);
}
static std::string cleanProtocolText(std::string v){
    for(char& c:v)if(c=='\r'||c=='\n'||c=='=')c=' ';
    if(v.size()>160)v.resize(160);
    return v;
}
static bool parseUnsigned(const std::map<std::string,std::string>& m,const char* key,unsigned& out){
    auto it=m.find(key);if(it==m.end()||it->second.empty())return false;
    try{std::size_t n=0;unsigned long v=std::stoul(it->second,&n,10);if(n!=it->second.size())return false;out=unsigned(v);return true;}catch(...){return false;}
}
static bool parseBool(const std::map<std::string,std::string>& m,const char* key,bool fallback=false){
    auto it=m.find(key);if(it==m.end())return fallback;auto v=it->second;std::transform(v.begin(),v.end(),v.begin(),[](unsigned char c){return char(std::tolower(c));});return v=="1"||v=="true"||v=="yes"||v=="on";
}
static std::array<std::uint16_t,4> parseMoves(const std::string& text){
    std::array<std::uint16_t,4> out{};std::stringstream ss(text);std::string part;int i=0;
    while(i<4&&std::getline(ss,part,',')){try{unsigned long v=std::stoul(trim(part));if(v<=65535)out[std::size_t(i)]=std::uint16_t(v);}catch(...){}++i;}return out;
}

static bool setNonblocking(HgSocket s,bool on){
#ifdef _WIN32
    u_long v=on?1UL:0UL;return ioctlsocket(s,FIONBIO,&v)==0;
#else
    int flags=fcntl(s,F_GETFL,0);if(flags<0)return false;return fcntl(s,F_SETFL,on?(flags|O_NONBLOCK):(flags&~O_NONBLOCK))==0;
#endif
}
static bool socketWouldBlock(){
#ifdef _WIN32
    int e=WSAGetLastError();return e==WSAEWOULDBLOCK||e==WSAEINPROGRESS||e==WSAEALREADY;
#else
    return errno==EINPROGRESS||errno==EWOULDBLOCK||errno==EAGAIN;
#endif
}
static HgSocket connectTcp(const HgMysteryGiftConfig& cfg,std::string& error){
    static SocketInit init;if(!init.ok){error="socket initialization failed";return HG_INVALID_SOCKET;}
    addrinfo hints{};hints.ai_family=AF_UNSPEC;hints.ai_socktype=SOCK_STREAM;hints.ai_protocol=IPPROTO_TCP;
    addrinfo* results=nullptr;std::string port=std::to_string(cfg.port);
    int gai=getaddrinfo(cfg.host.c_str(),port.c_str(),&hints,&results);if(gai!=0||!results){error="could not resolve server host";return HG_INVALID_SOCKET;}
    HgSocket connected=HG_INVALID_SOCKET;
    for(addrinfo* ai=results;ai;ai=ai->ai_next){
        HgSocket s=::socket(ai->ai_family,ai->ai_socktype,ai->ai_protocol);if(s==HG_INVALID_SOCKET)continue;
        if(!setNonblocking(s,true)){hg_close_socket(s);continue;}
        int rc=::connect(s,ai->ai_addr,
#ifdef _WIN32
            int(ai->ai_addrlen)
#else
            ai->ai_addrlen
#endif
        );
        if(rc!=0&&!socketWouldBlock()){hg_close_socket(s);continue;}
        if(rc!=0){
            fd_set wfds;FD_ZERO(&wfds);FD_SET(s,&wfds);timeval tv{cfg.timeoutMs/1000,(cfg.timeoutMs%1000)*1000};
#ifdef _WIN32
            rc=select(0,nullptr,&wfds,nullptr,&tv);
#else
            rc=select(s+1,nullptr,&wfds,nullptr,&tv);
#endif
            if(rc<=0){hg_close_socket(s);continue;}
            int soerr=0;
#ifdef _WIN32
            int slen=sizeof(soerr);
#else
            socklen_t slen=sizeof(soerr);
#endif
            if(getsockopt(s,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&soerr),&slen)!=0||soerr!=0){hg_close_socket(s);continue;}
        }
        setNonblocking(s,false);
#ifdef _WIN32
        DWORD timeout=DWORD(std::max(250,cfg.timeoutMs));setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout));setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout));
#else
        timeval timeout{cfg.timeoutMs/1000,(cfg.timeoutMs%1000)*1000};setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
#endif
        connected=s;break;
    }
    freeaddrinfo(results);if(connected==HG_INVALID_SOCKET)error="connection failed or timed out";return connected;
}
static bool sendAll(HgSocket s,const std::string& data){
    std::size_t sent=0;while(sent<data.size()){
#ifdef _WIN32
        int n=::send(s,data.data()+sent,int(std::min<std::size_t>(data.size()-sent,0x7fffffff)),0);
#else
        ssize_t n=::send(s,data.data()+sent,data.size()-sent,0);
#endif
        if(n<=0)return false;
        sent+=std::size_t(n);
    }return true;
}
static bool requestResponse(const HgMysteryGiftConfig& cfg,const std::string& request,std::map<std::string,std::string>& fields,std::string& error){
    HgSocket s=connectTcp(cfg,error);if(s==HG_INVALID_SOCKET)return false;
    if(!sendAll(s,request)){error="failed to send request";hg_close_socket(s);return false;}
    std::string data;std::array<char,1024> buf{};
    while(data.size()<32768){
#ifdef _WIN32
        int n=recv(s,buf.data(),int(buf.size()),0);
#else
        ssize_t n=recv(s,buf.data(),buf.size(),0);
#endif
        if(n<=0)break;
        data.append(buf.data(),std::size_t(n));
        if(data.find("\nEND\n")!=std::string::npos||data.rfind("END\n",0)==0)break;
    }
    hg_close_socket(s);if(data.empty()){error="server returned no response";return false;}
    std::stringstream ss(data);std::string line;bool protocol=false,ended=false;
    while(std::getline(ss,line)){line=trim(line);if(line.empty())continue;if(line=="HGSS-MG/1"){protocol=true;continue;}if(line=="END"){ended=true;break;}auto p=line.find('=');if(p!=std::string::npos)fields[trim(line.substr(0,p))]=trim(line.substr(p+1));}
    if(!protocol||!ended){error="invalid Mystery Gift server response";return false;}return true;
}
}

HgMysteryGiftConfig hg_load_mystery_gift_config(const std::filesystem::path& savePath){
    // Production Mystery Gift endpoint is intentionally baked into this build.
    // Ignore legacy per-user config/environment overrides so an old localhost
    // test configuration cannot redirect retail users away from the public
    // Playit.gg tunnel. The savePath argument is retained for API compatibility.
    (void)savePath;
    return HgMysteryGiftConfig{};
}

std::string hg_new_mystery_gift_save_id(){
    std::random_device rd;
    std::mt19937_64 gen((std::uint64_t(rd())<<32)^rd()^std::uint64_t(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    std::ostringstream ss;
    ss<<std::hex<<std::setfill('0')<<std::setw(16)<<gen()<<std::setw(16)<<gen();
    return ss.str();
}

std::string hg_mystery_gift_client_id(const std::filesystem::path& savePath){
    std::filesystem::path p=savePath.has_parent_path()?savePath.parent_path()/"mystery_gift_client_id.txt":std::filesystem::path("mystery_gift_client_id.txt");
    if(std::ifstream in(p);in){std::string id;std::getline(in,id);id=trim(id);if(id.size()>=16&&id.size()<=80)return id;}
    std::string id=hg_new_mystery_gift_save_id();
    try{if(p.has_parent_path())std::filesystem::create_directories(p.parent_path());std::ofstream o(p);if(o)o<<id<<"\n";}catch(...){}
    return id;
}

HgMysteryGiftFetchResult hg_fetch_mystery_gift(const HgMysteryGiftConfig& cfg,const std::string& clientId,const std::string& playerName,const std::unordered_set<std::string>& receivedGiftIds){
    HgMysteryGiftFetchResult r;std::ostringstream req;req<<"HGSS-MG/1\nACTION=FETCH\nCLIENT="<<cleanProtocolText(clientId)<<"\nPLAYER="<<cleanProtocolText(playerName)<<"\n";
    if(!receivedGiftIds.empty()){
        std::vector<std::string> ids(receivedGiftIds.begin(),receivedGiftIds.end());std::sort(ids.begin(),ids.end());
        std::ostringstream joined;bool first=true;for(auto const& id:ids){if(id.empty())continue;if(!first)joined<<',';first=false;auto v=cleanProtocolText(id);std::replace(v.begin(),v.end(),',','_');joined<<v;}
        req<<"RECEIVED="<<joined.str()<<"\n";
    }
    req<<"END\n";
    std::map<std::string,std::string> f;std::string err;if(!requestResponse(cfg,req.str(),f,err)){r.message=err;return r;}r.transportOk=true;r.status=f["STATUS"];r.message=f["MESSAGE"];
    if(r.status=="ALREADY_CLAIMED"){r.alreadyClaimed=true;return r;}if(r.status!="GIFT"){if(r.message.empty())r.message="No gift is currently available.";return r;}
    r.gift.id=f["GIFT_ID"];r.gift.title=f["TITLE"];r.gift.message=f["MESSAGE"];if(r.gift.id.empty()){r.message="Server gift is missing an ID.";return r;}
    if(f["TYPE"]=="ITEM"){
        unsigned item=0,qty=0;if(!parseUnsigned(f,"ITEM_ID",item)||!parseUnsigned(f,"QUANTITY",qty)||item==0||item>65535||qty==0||qty>999){r.message="Server returned an invalid item gift.";return r;}
        r.gift.type=HgMysteryGiftType::Item;r.gift.itemId=std::uint16_t(item);r.gift.quantity=std::uint16_t(qty);r.hasGift=true;return r;
    }
    if(f["TYPE"]=="POKEMON"){
        unsigned species=0,level=1;if(!parseUnsigned(f,"SPECIES",species)||species==0||species>493){r.message="Server returned an invalid Pokemon species.";return r;}parseUnsigned(f,"LEVEL",level);level=std::clamp(level,1u,100u);
        unsigned held=0,form=0,ability=0;parseUnsigned(f,"HELD_ITEM",held);parseUnsigned(f,"FORM",form);parseUnsigned(f,"ABILITY",ability);
        HgMon m=hg_make_mon(std::uint16_t(species),std::uint8_t(level),std::uint16_t(std::min(held,65535u)),std::uint8_t(std::min(form,255u)),std::uint8_t(std::min(ability,255u)));
        auto it=f.find("MOVES");if(it!=f.end()){m.moves=parseMoves(it->second);m.pp={0,0,0,0};m.maxPp={0,0,0,0};hg_rehydrate_mon(m);}
        unsigned v=0;if(parseUnsigned(f,"MAX_HP",v))m.maxHp=std::uint16_t(std::clamp(v,1u,65535u));if(parseUnsigned(f,"HP",v))m.hp=std::uint16_t(std::clamp(v,0u,unsigned(m.maxHp)));else m.hp=m.maxHp;
        if(parseUnsigned(f,"ATTACK",v))m.attack=std::uint16_t(std::clamp(v,1u,65535u));
        if(parseUnsigned(f,"DEFENSE",v))m.defense=std::uint16_t(std::clamp(v,1u,65535u));
        if(parseUnsigned(f,"SP_ATTACK",v))m.spAttack=std::uint16_t(std::clamp(v,1u,65535u));
        if(parseUnsigned(f,"SP_DEFENSE",v))m.spDefense=std::uint16_t(std::clamp(v,1u,65535u));
        if(parseUnsigned(f,"SPEED",v))m.speed=std::uint16_t(std::clamp(v,1u,65535u));
        if(parseUnsigned(f,"EXP",v))m.exp=v;
        if(parseUnsigned(f,"FRIENDSHIP",v))m.friendship=std::uint8_t(std::min(v,255u));
        if(parseUnsigned(f,"GENDER",v))m.gender=std::uint8_t(std::min(v,2u));
        if(auto n=f.find("NICKNAME");n!=f.end()&&!n->second.empty())m.nickname=n->second.substr(0,20);
        m.egg=parseBool(f,"EGG",false);m.mine=true;
        r.gift.type=HgMysteryGiftType::Pokemon;r.gift.pokemon=std::move(m);r.hasGift=true;return r;
    }
    r.message="Server returned an unsupported gift type.";return r;
}

bool hg_ack_mystery_gift(const HgMysteryGiftConfig& cfg,const std::string& clientId,const std::string& giftId,std::string* error){
    std::ostringstream req;req<<"HGSS-MG/1\nACTION=ACK\nCLIENT="<<cleanProtocolText(clientId)<<"\nGIFT_ID="<<cleanProtocolText(giftId)<<"\nEND\n";std::map<std::string,std::string> f;std::string err;
    if(!requestResponse(cfg,req.str(),f,err)){if(error)*error=err;return false;}if(f["STATUS"]!="ACKED"){if(error)*error=f["MESSAGE"].empty()?"server did not accept acknowledgement":f["MESSAGE"];return false;}return true;
}

bool hg_apply_mystery_gift(HgGameState& state,const HgMysteryGiftPayload& gift,std::string* description){
    if(gift.id.empty()||state.mysteryGiftClaims.count(gift.id)){if(description)*description="This gift has already been received.";return false;}
    if(gift.type==HgMysteryGiftType::Item){
        if(!state.addItem(gift.itemId,gift.quantity)){if(description)*description="There is not enough room in the Bag for this gift.";return false;}
        state.mysteryGiftClaims.insert(gift.id);if(description)*description="Received "+hg_item_name(gift.itemId)+" x"+std::to_string(gift.quantity)+"!";return true;
    }
    if(gift.type==HgMysteryGiftType::Pokemon){
        HgMon mon=gift.pokemon;if(!mon.species||!state.storeMon(mon)){if(description)*description="There is no room for this Pokemon in the party or PC.";return false;}
        state.own(mon.species);state.mysteryGiftClaims.insert(gift.id);if(description)*description="Received "+(mon.nickname.empty()?hg_species_name(mon.species):mon.nickname)+"!";return true;
    }
    if(description)*description="The server gift type is invalid.";
    return false;
}
