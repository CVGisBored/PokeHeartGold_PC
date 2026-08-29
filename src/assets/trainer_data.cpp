#include "assets/trainer_data.hpp"
#include "assets/nsbmd.hpp"
#include <algorithm>

namespace {
std::uint16_t u16(const std::vector<unsigned char>& b,std::size_t p){return p+1<b.size()?std::uint16_t(b[p])|(std::uint16_t(b[p+1])<<8):0;}
std::uint32_t u32(const std::vector<unsigned char>& b,std::size_t p){return p+3<b.size()?std::uint32_t(b[p])|(std::uint32_t(b[p+1])<<8)|(std::uint32_t(b[p+2])<<16)|(std::uint32_t(b[p+3])<<24):0;}
}

HgTrainerRecord load_hg_trainer(const std::filesystem::path& root,std::uint16_t id){
    HgTrainerRecord out;out.id=id;
    auto data=read_narc_member(root/"a/0/5/5",id);
    auto party=read_narc_member(root/"a/0/5/6",id);
    if(data.size()!=20){out.error="trainer data member is not 20 bytes";return out;}
    out.partyFlags=data[0];out.trainerClass=data[1];out.battleType=data[2];out.partyCount=data[3];
    for(int i=0;i<4;i++)out.items[std::size_t(i)]=u16(data,4+std::size_t(i)*2);
    out.aiFlags=u32(data,12);out.extra=u32(data,16);
    const bool customMoves=(out.partyFlags&1)!=0,heldItems=(out.partyFlags&2)!=0;
    const std::size_t stride=8+(heldItems?2:0)+(customMoves?8:0);
    if(stride==0||party.size()<stride*std::size_t(out.partyCount)){out.error="trainer party member is truncated";return out;}
    out.party.reserve(out.partyCount);std::size_t p=0;
    for(unsigned i=0;i<out.partyCount;i++){
        HgTrainerMonRecord m;m.difficulty=u16(party,p);m.level=u16(party,p+2);m.species=u16(party,p+4);m.capsule=u16(party,p+6);p+=8;
        if(heldItems){m.heldItem=u16(party,p);p+=2;}
        if(customMoves){for(int k=0;k<4;k++)m.moves[std::size_t(k)]=u16(party,p+std::size_t(k)*2);p+=8;}
        out.party.push_back(m);
    }
    out.valid=true;return out;
}
