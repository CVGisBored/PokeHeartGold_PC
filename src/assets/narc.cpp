#include "assets/narc.hpp"
#include <fstream>
#include <vector>
#include <cstring>

namespace {
std::uint16_t u16(const std::vector<unsigned char>& b,std::size_t p){if(p+2>b.size())return 0;return std::uint16_t(b[p])|std::uint16_t(b[p+1])<<8;}
std::uint32_t u32(const std::vector<unsigned char>& b,std::size_t p){if(p+4>b.size())return 0;return std::uint32_t(b[p])|std::uint32_t(b[p+1])<<8|std::uint32_t(b[p+2])<<16|std::uint32_t(b[p+3])<<24;}
}
NarcArchiveInfo inspect_narc(const std::filesystem::path& path){
    NarcArchiveInfo out;std::ifstream f(path,std::ios::binary);if(!f)return out;f.seekg(0,std::ios::end);auto n=f.tellg();if(n<16)return out;f.seekg(0);std::vector<unsigned char>b(static_cast<std::size_t>(n));f.read(reinterpret_cast<char*>(b.data()),static_cast<std::streamsize>(b.size()));if(!f||std::memcmp(b.data(),"NARC",4)!=0)return out;
    out.fileSize=u32(b,8);out.blockCount=u16(b,14);std::size_t p=16,fat=0,img=0;for(unsigned i=0;i<out.blockCount&&p+8<=b.size();i++){std::uint32_t sz=u32(b,p+4);if(sz<8||p+sz>b.size())return {};if(std::memcmp(b.data()+p,"BTAF",4)==0)fat=p;else if(std::memcmp(b.data()+p,"GMIF",4)==0)img=p;p+=sz;}
    if(!fat||!img) return out;
    std::uint16_t count=u16(b,fat+8);
    std::size_t data0=img+8;
    if(fat+12+std::size_t(count)*8>b.size()) return {};
    out.members.reserve(count);
    for(unsigned i=0;i<count;i++){
        std::uint32_t s=u32(b,fat+12+i*8), e=u32(b,fat+16+i*8);
        if(e<s||data0+e>b.size()) return {};
        out.members.push_back({s,e-s});
    }
    out.valid=true;
    return out;
}
