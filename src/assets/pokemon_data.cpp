#include "assets/pokemon_data.hpp"
#include "assets/hg_text.hpp"
#include "assets/nsbmd.hpp"
#include <algorithm>
#include <array>

namespace {
std::vector<HgPersonalData> gPersonal;
std::vector<HgMoveData> gMoves;
std::vector<HgItemData> gItems;
std::vector<std::vector<std::uint16_t>> gLearnsets;
std::vector<std::string> gSpeciesNames,gMoveNames,gItemNames;
std::array<std::array<std::uint32_t,101>,8> gGrowth{};
std::array<bool,8> gGrowthValid{};
std::uint16_t u16(const std::vector<unsigned char>& b,std::size_t p){return p+1<b.size()?std::uint16_t(b[p])|(std::uint16_t(b[p+1])<<8):0;}
std::uint32_t u32(const std::vector<unsigned char>& b,std::size_t p){return p+3<b.size()?std::uint32_t(b[p])|(std::uint32_t(b[p+1])<<8)|(std::uint32_t(b[p+2])<<16)|(std::uint32_t(b[p+3])<<24):0;}
std::string upper(std::string s){for(char& c:s)if(c>='a'&&c<='z')c=char(c-'a'+'A');return s;}
std::size_t itemDataMember(std::uint16_t item){
    // Retail sItemNarcIds[] collapses the 22 unused IDs 113..134 onto
    // member 0, then HGSS's unused Explorer Kit (428) does the same.
    if(item<=112)return item;
    if(item<=134)return 0;
    if(item<=427)return std::size_t(item-22);
    if(item==428)return 0;
    return std::size_t(item-23);
}
void loadNames(const std::filesystem::path& assets,std::size_t bank,std::vector<std::string>& out,std::size_t wanted){
    HgMessageBank mb(read_narc_member(assets/"a/0/2/7",bank));out.assign(wanted,{});
    if(!mb.valid())return;
    for(std::size_t i=0;i<std::min(wanted,mb.count());++i){
        auto d=mb.decode(i);
        if(d.valid)out[i]=upper(d.text);
    }
}
}

bool hg_initialize_pokemon_database(const std::filesystem::path& assets){
    auto pa=assets/"a/0/0/2", ma=assets/"a/0/1/1", la=assets/"a/0/3/3", ia=assets/"a/0/1/7", ga=assets/"a/0/0/3";
    gPersonal.assign(508,{});for(std::size_t i=0;i<gPersonal.size();++i){auto b=read_narc_member(pa,i);if(b.size()!=44)continue;HgPersonalData p;p.valid=true;p.hp=b[0];p.attack=b[1];p.defense=b[2];p.speed=b[3];p.spAttack=b[4];p.spDefense=b[5];p.type1=b[6];p.type2=b[7];p.catchRate=b[8];p.baseExp=b[9];p.item1=u16(b,12);p.item2=u16(b,14);p.genderRatio=b[16];p.eggCycles=b[17];p.baseFriendship=b[18];p.growthRate=b[19];p.ability1=b[22];p.ability2=b[23];gPersonal[i]=p;}
    gGrowthValid.fill(false);for(std::size_t rate=0;rate<gGrowth.size();++rate){auto b=read_narc_member(ga,rate);if(b.size()!=101u*4u)continue;for(std::size_t level=0;level<=100;++level)gGrowth[rate][level]=u32(b,level*4u);gGrowthValid[rate]=true;}
    gMoves.assign(471,{});for(std::size_t i=0;i<gMoves.size();++i){auto b=read_narc_member(ma,i);if(b.size()!=16)continue;HgMoveData m;m.valid=true;m.effect=u16(b,0);m.category=b[2];m.power=b[3];m.type=b[4];m.accuracy=b[5];m.pp=b[6];m.effectChance=b[7];m.priority=static_cast<std::int8_t>(b[10]);gMoves[i]=m;}
    // Retail itemtool/itemdata/item_data.narc is a/0/1/7 in the US HG ROM.
    // Each 34-byte ItemData member begins with price; the packed u16 at +8
    // stores natural-gift flags followed by field/battle pocket bitfields.
    gItems.assign(537,{});for(std::size_t i=0;i<gItems.size();++i){auto b=read_narc_member(ia,itemDataMember(static_cast<std::uint16_t>(i)));if(b.size()!=34)continue;HgItemData it;it.valid=true;it.price=u16(b,0);auto packed=u16(b,8);it.fieldPocket=std::uint8_t((packed>>7)&0x0f);it.battlePocket=std::uint8_t((packed>>11)&0x1f);it.fieldUseFunc=b[10];it.battleUseFunc=b[11];it.partyUse=b[12];gItems[i]=it;}
    gLearnsets.assign(508,{});for(std::size_t i=0;i<gLearnsets.size();++i){auto b=read_narc_member(la,i);for(std::size_t p=0;p+1<b.size();p+=2){auto v=u16(b,p);if(v==0xffff)break;std::uint16_t move=v&0x1ffu;std::uint8_t level=std::uint8_t(v>>9);gLearnsets[i].push_back(std::uint16_t((std::uint16_t(level)<<9)|move));}}
    loadNames(assets,237,gSpeciesNames,496);loadNames(assets,751,gMoveNames,468);loadNames(assets,222,gItemNames,537);
    return gPersonal.size()>493&&gPersonal[1].valid&&gMoves.size()>467&&gMoves[33].valid&&gItems.size()>4&&gItems[4].valid;
}
const HgPersonalData* hg_personal_data(std::uint16_t s){return s<gPersonal.size()&&gPersonal[s].valid?&gPersonal[s]:nullptr;}
const HgMoveData* hg_move_data(std::uint16_t m){return m<gMoves.size()&&gMoves[m].valid?&gMoves[m]:nullptr;}
const HgItemData* hg_item_data(std::uint16_t i){return i<gItems.size()&&gItems[i].valid?&gItems[i]:nullptr;}
std::vector<std::uint16_t> hg_levelup_moves(std::uint16_t s,std::uint8_t level){std::vector<std::uint16_t> out;if(s>=gLearnsets.size())return out;for(auto v:gLearnsets[s])if((v>>9)<=level){auto m=v&0x1ffu;if(m){out.erase(std::remove(out.begin(),out.end(),m),out.end());out.push_back(m);if(out.size()>4)out.erase(out.begin());}}return out;}
std::string hg_rom_species_name(std::uint16_t s){if(s<gSpeciesNames.size()&&!gSpeciesNames[s].empty())return gSpeciesNames[s];return "POKEMON "+std::to_string(s);}
std::string hg_rom_move_name(std::uint16_t m){if(m<gMoveNames.size()&&!gMoveNames[m].empty())return gMoveNames[m];return "MOVE "+std::to_string(m);}
std::string hg_rom_item_name(std::uint16_t i){if(i<gItemNames.size()&&!gItemNames[i].empty())return gItemNames[i];return "ITEM "+std::to_string(i);}
std::size_t hg_species_count(){return gPersonal.empty()?0:493;}
std::size_t hg_move_count(){return gMoves.empty()?0:467;}
std::size_t hg_item_count(){return gItems.empty()?0:gItems.size()-1;}
std::uint32_t hg_exp_for_level(std::uint16_t species,std::uint8_t level){
    if(level>100)level=100;
    auto* p=hg_personal_data(species);if(!p||p->growthRate>=gGrowth.size()||!gGrowthValid[p->growthRate])return std::uint32_t(level)*level*level;
    return gGrowth[p->growthRate][level];
}
std::uint8_t hg_level_for_exp(std::uint16_t species,std::uint32_t exp){
    auto* p=hg_personal_data(species);if(!p||p->growthRate>=gGrowth.size()||!gGrowthValid[p->growthRate]){std::uint8_t level=1;while(level<100&&std::uint32_t(level+1)*(level+1)*(level+1)<=exp)++level;return level;}
    std::uint8_t level=1;while(level<100&&gGrowth[p->growthRate][level+1]<=exp)++level;return level;
}
const char* hg_type_name(std::uint8_t t){static const char* n[]={"NORMAL","FIGHT","FLYING","POISON","GROUND","ROCK","BUG","GHOST","STEEL","???","FIRE","WATER","GRASS","ELECTRIC","PSYCHIC","ICE","DRAGON","DARK"};return t<18?n[t]:"???";}
float hg_type_effectiveness(std::uint8_t a,std::uint8_t d){
    // Gen IV type chart, indexed Normal..Dark (including the unused ??? slot 9).
    static const std::array<std::array<std::uint8_t,18>,18> c={{
      {{2,2,2,2,2,1,2,0,1,2,2,2,2,2,2,2,2,2}},{{4,2,1,1,2,4,1,0,4,2,2,2,2,2,1,4,2,4}},
      {{2,4,2,2,2,1,4,2,1,2,2,2,4,1,2,2,2,2}},{{2,2,2,1,1,1,2,1,0,2,2,2,4,2,2,2,2,2}},
      {{2,2,0,4,2,4,1,2,4,2,4,2,1,4,2,2,2,2}},{{2,1,4,2,1,2,4,2,1,2,4,2,2,2,2,4,2,2}},
      {{2,1,1,1,2,2,2,1,1,2,1,2,4,2,4,2,2,4}},{{0,2,2,2,2,2,2,4,1,2,2,2,2,2,4,2,2,1}},
      {{2,2,2,2,2,4,2,2,1,2,1,1,2,1,2,4,2,2}},{{2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2}},
      {{2,2,2,2,2,1,4,2,4,2,1,1,4,2,2,4,1,2}},{{2,2,2,2,4,4,2,2,2,2,4,1,1,2,2,2,1,2}},
      {{2,2,1,1,4,4,1,2,1,2,1,4,1,2,2,2,1,2}},{{2,2,4,2,0,2,2,2,2,2,2,4,1,1,2,2,1,2}},
      {{2,4,2,4,2,2,2,2,1,2,2,2,2,2,1,2,2,0}},{{2,2,4,2,4,2,2,2,1,2,1,1,4,2,2,1,4,2}},
      {{2,2,2,2,2,2,2,2,1,2,2,2,2,2,2,2,4,2}},{{2,1,2,2,2,2,2,4,1,2,2,2,2,2,4,2,2,1}}
    }};return a<18&&d<18?float(c[a][d])*0.5f:1.0f;
}
