#include "assets/hg_text.hpp"
#include <algorithm>
#include <sstream>

namespace {
std::uint16_t u16(const std::vector<unsigned char>& b,std::size_t p){return p+2<=b.size()?std::uint16_t(b[p])|(std::uint16_t(b[p+1])<<8):0;}
std::uint32_t u32(const std::vector<unsigned char>& b,std::size_t p){return p+4<=b.size()?std::uint32_t(b[p])|(std::uint32_t(b[p+1])<<8)|(std::uint32_t(b[p+2])<<16)|(std::uint32_t(b[p+3])<<24):0;}
std::string controlText(std::uint16_t fn,const std::vector<std::uint16_t>& args,const std::string& player,const std::vector<std::string>& vars){
    // HG text control 0x0103 is the player name in the retail message stream.
    if(fn==0x0103)return player;
    // STRVAR_1. The first control argument selects the string-format slot.
    if(fn==0x0100){std::size_t i=args.empty()?0:std::size_t(args[0]);if(i<vars.size()&&!vars[i].empty())return vars[i];return "<STRVAR>";}
    if(fn==0x0205||fn==0x0206)return {}; // alignment controls
    return {};
}
void finishPage(std::vector<std::string>& pages,std::string& cur){
    while(!cur.empty()&&(cur.back()=='\n'||cur.back()==' '))cur.pop_back();
    if(!cur.empty())pages.push_back(cur);
    cur.clear();
}
}

HgMessageBank::HgMessageBank(std::vector<unsigned char> bytes):bytes_(std::move(bytes)){
    if(bytes_.size()<4)return;
    count_=u16(bytes_,0);key_=u16(bytes_,2);
    valid_=count_>0&&4+std::size_t(count_)*8<=bytes_.size();
}

std::string hg_ascii_from_code(std::uint16_t c){
    if(c>=0x0121&&c<=0x012a)return std::string(1,char('0'+(c-0x0121)));
    if(c>=0x012b&&c<=0x0144)return std::string(1,char('A'+(c-0x012b)));
    if(c>=0x0145&&c<=0x015e)return std::string(1,char('a'+(c-0x0145)));
    switch(c){
        case 0x01de: case 0x01e2:return " ";
        case 0x01ab:return "!"; case 0x01ac:return "?"; case 0x01ad:return ","; case 0x01ae:return ".";
        case 0x01af:return "..."; case 0x01b0:return "."; case 0x01b1:return "/";
        case 0x01b2: case 0x01b3:return "'"; case 0x01b4: case 0x01b5: case 0x01b6:return "\"";
        case 0x01b9:return "("; case 0x01ba:return ")"; case 0x01bb:return "M"; case 0x01bc:return "F";
        case 0x01bd:return "+"; case 0x01be:return "-"; case 0x01bf:return "*"; case 0x01c0:return "#"; case 0x01c1:return "=";
        case 0x01c2:return "&"; case 0x01c3:return "~"; case 0x01c4:return ":"; case 0x01c5:return ";";
        case 0x01d0:return "@"; case 0x01d2:return "%"; case 0x01e8:return "deg"; case 0x01e9:return "_";
        // Common Latin glyphs appearing in English HG text. Keep the renderer ASCII-safe.
        case 0x0188:return "e"; case 0x0167:return "E"; case 0x0170:return "N"; case 0x0190:return "n";
        default:return {};
    }
}

HgDecodedMessage HgMessageBank::decode(std::size_t index,const std::string& playerName,const std::vector<std::string>& strvars) const{
    HgDecodedMessage out;if(!valid_||index>=count_)return out;
    std::uint32_t off=u32(bytes_,4+index*8),len=u32(bytes_,8+index*8);
    std::uint32_t seed=(std::uint32_t(key_)*765u*std::uint32_t(index+1))&0xffffu;seed|=seed<<16;off^=seed;len^=seed;
    if(len>0x10000u||off+len*2ull>bytes_.size())return out;
    std::vector<std::uint16_t> w(len);std::uint16_t s=std::uint16_t((index+1)*596947u);
    for(std::size_t i=0;i<len;i++){w[i]=u16(bytes_,off+i*2)^s;s=std::uint16_t(s+18749u);}
    std::string cur,all;
    for(std::size_t i=0;i<w.size();i++){
        std::uint16_t c=w[i];if(c==0xffff)break;
        if(c==0xfffe&&i+2<w.size()){
            std::uint16_t fn=w[++i],argc=w[++i];std::vector<std::uint16_t> args;args.reserve(argc);
            for(unsigned a=0;a<argc&&i+1<w.size();a++)args.push_back(w[++i]);
            std::string t=controlText(fn,args,playerName,strvars);cur+=t;all+=t;continue;
        }
        if(c==0xe000){cur+='\n';all+='\n';continue;}
        if(c==0x25bc){cur+='\n';all+='\n';continue;}
        if(c==0x25bd){all+='\f';finishPage(out.pages,cur);continue;}
        auto t=hg_ascii_from_code(c);cur+=t;all+=t;
    }
    finishPage(out.pages,cur);if(out.pages.empty()&&!all.empty())out.pages.push_back(all);
    out.text=std::move(all);out.valid=true;return out;
}
