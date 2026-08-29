#include "game/retail_mart.hpp"
#include <algorithm>
#include <array>

namespace {
using I=HgRetailMartItem;

template<std::size_t N>
std::vector<I> ids(const std::array<std::uint16_t,N>& a,std::uint32_t price=0){
    std::vector<I> out;out.reserve(N);
    for(std::size_t i=0;i<N;i++)out.push_back({a[i],price,std::uint16_t(i)});
    return out;
}

// SpecialMartBuy table from retail HG/SS ScrCmd_SpecialMartBuy.
static const std::vector<std::vector<std::uint16_t>> kSpecial={
    {146,14}, {141,14,6}, {140,14,6},
    {17,26,25,24,28,18,22,19,20,21,27},
    {4,3,2,78,63,79,76,77,137,138,139,145},
    {59,57,58,55,56,60,61,62},
    {46,47,49,52,48,45},
    {397,344,381,410,343,360,349,379,365,352,341,342},
    {36,34,35,37}, {146,14,6}, {143,14,6},
    {17,26,25,27,28}, {146,6,13},
    {2,77,25,24,23,28,27}, {146,8,13,15}, {146,13,15},
    {144,13,15}, {146,15},
    {17,26,25,24,28,18,22,19,20,21,27},
    {4,3,2,78,63,79,76,77,137,138,139,145},
    {348,354,414,405,339,368,347,355,403,382,399,406},
    {146,141,140}, {59,57,58,55,56,60,61,62},
    {46,47,49,52,48,45}, {142,13,15}, {142,8,15}, {142,6,14},
    {63,30,31,32,79,143}, {86,4,17},
    {3,26,25,18,22,76,28,146}
};

struct Priced { std::uint16_t id; std::uint32_t price; };
using PList=std::vector<Priced>;
static const std::array<PList,14> kPokeathlon={{
    {{485,200},{487,200},{491,200},{33,100},{221,3000},{93,1000}},
    {{485,200},{487,200},{488,200},{33,100},{81,3000},{50,2000}},
    {{486,200},{489,200},{490,200},{33,100},{82,2500},{51,1000}},
    {{487,200},{489,200},{491,200},{33,100},{84,2500},{93,1000}},
    {{486,200},{489,200},{490,200},{33,100},{83,2500},{51,1000}},
    {{485,200},{486,200},{488,200},{33,100},{233,2500},{92,500}},
    {{488,200},{490,200},{491,200},{33,100},{85,2500},{50,2000}},
    {{485,200},{487,200},{491,200},{33,100},{221,3000},{93,1000},{23,500},{92,500},{80,3000},{82,2500},{107,3000},{109,3000}},
    {{485,200},{487,200},{488,200},{33,100},{81,3000},{50,2000},{23,500},{221,3000},{80,3000},{84,2500},{107,3000},{108,3000}},
    {{486,200},{489,200},{490,200},{33,100},{82,2500},{51,1000},{23,500},{233,2500},{84,2500},{85,2500},{108,3000},{109,3000}},
    {{487,200},{489,200},{491,200},{33,100},{84,2500},{93,1000},{23,500},{235,2500},{83,2500},{81,3000},{107,3000},{109,3000}},
    {{486,200},{489,200},{490,200},{33,100},{83,2500},{51,1000},{23,500},{221,3000},{82,2500},{85,2500},{107,3000},{108,3000}},
    {{485,200},{486,200},{488,200},{33,100},{233,2500},{92,500},{23,500},{235,2500},{84,2500},{80,3000},{108,3000},{109,3000}},
    {{488,200},{490,200},{491,200},{33,100},{85,2500},{50,2000},{23,500},{233,2500},{83,2500},{107,3000},{108,3000},{109,3000}}
}};

static const std::array<PList,5> kDataCards={{
    {{505,500},{506,500},{507,1000},{508,1000},{509,500},{510,500}},
    {{511,1000},{512,1000},{513,1000},{514,1000},{515,1000},{516,1000}},
    {{517,1500},{518,1500},{519,1500},{520,1000},{521,1000},{522,1000}},
    {{523,500},{524,500},{525,2000},{526,2000},{527,1000},{528,1000}},
    {{529,2000},{530,3000},{531,9999}}
}};
}

std::vector<HgRetailMartItem> hg_standard_mart_inventory(unsigned badgeCount){
    unsigned tier=badgeCount==0?1:badgeCount<=2?2:badgeCount<=4?3:badgeCount<=6?4:badgeCount==7?5:6;
    static constexpr std::array<std::pair<std::uint16_t,unsigned>,19> table={{
        {4,1},{3,3},{2,4},{17,1},{26,2},{25,4},{24,5},{23,6},{28,3},
        {18,1},{22,1},{21,2},{19,2},{20,2},{27,4},{78,2},{79,2},{76,3},{77,4}
    }};
    std::vector<HgRetailMartItem> out;
    for(auto [id,required]:table)if(required<=tier)out.push_back({id,0,std::uint16_t(out.size())});
    return out;
}

std::vector<HgRetailMartItem> hg_special_mart_inventory(std::uint16_t which){
    if(which>=kSpecial.size())return {};
    std::vector<HgRetailMartItem> out;out.reserve(kSpecial[which].size());
    for(std::size_t i=0;i<kSpecial[which].size();i++)out.push_back({kSpecial[which][i],0,std::uint16_t(i)});
    return out;
}
std::vector<HgRetailMartItem> hg_decoration_mart_inventory(std::uint16_t which){
    static constexpr std::array<std::uint16_t,5> a={7,22,25,26,27};
    static constexpr std::array<std::uint16_t,6> b={115,116,117,119,120,121};
    return which==0?ids(a,100):which==1?ids(b,100):std::vector<HgRetailMartItem>{};
}
std::vector<HgRetailMartItem> hg_seal_mart_inventory(std::uint16_t which){
    static constexpr std::array<std::array<std::uint16_t,7>,7> s={{
        {{1,8,29,43,15,22,36}},{{2,9,30,37,44,16,23}},{{3,10,31,38,45,17,24}},
        {{4,25,32,39,46,11,18}},{{26,33,40,47,5,12,19}},{{27,34,41,48,6,13,20}},{{7,49,28,42,14,21,35}}
    }};
    return which<s.size()?ids(s[which],100):std::vector<HgRetailMartItem>{};
}
std::vector<HgRetailMartItem> hg_pokeathlon_daily_inventory(unsigned weekday,bool nationalDex){
    const unsigned idx=std::min(weekday,6u)+(nationalDex?7u:0u);
    std::vector<HgRetailMartItem> out;out.reserve(kPokeathlon[idx].size());
    for(std::size_t i=0;i<kPokeathlon[idx].size();i++)out.push_back({kPokeathlon[idx][i].id,kPokeathlon[idx][i].price,std::uint16_t(i)});
    return out;
}
std::vector<HgRetailMartItem> hg_pokeathlon_data_card_inventory(unsigned purchasedCount){
    const unsigned tier=std::min<unsigned>(4,purchasedCount/6);
    std::vector<HgRetailMartItem> out;out.reserve(kDataCards[tier].size());
    for(std::size_t i=0;i<kDataCards[tier].size();i++)out.push_back({kDataCards[tier][i].id,kDataCards[tier][i].price,std::uint16_t(i)});
    return out;
}

std::string hg_decoration_name(std::uint16_t id){
    switch(id){case 7:return "YELLOW CUSHION";case 22:return "CUPBOARD";case 25:return "TV";case 26:return "REFRIGERATOR";case 27:return "PRETTY SINK";case 115:return "MUNCHLAX DOLL";case 116:return "BONSLY DOLL";case 117:return "MIME JR. DOLL";case 119:return "MANTYKE DOLL";case 120:return "BUIZEL DOLL";case 121:return "CHATOT DOLL";default:return "DECORATION "+std::to_string(id);}
}
std::string hg_seal_name(std::uint16_t id){
    struct R{std::uint16_t first,last;const char* name;};
    static constexpr R ranges[]={{1,6,"HEART"},{7,12,"STAR"},{13,16,"LINE"},{17,20,"SMOKE"},{21,24,"ELE"},{25,28,"FOAMY"},{29,32,"FIRE"},{33,36,"PARTY"},{37,42,"FLORA"},{43,49,"SONG"}};
    for(auto const&r:ranges)if(id>=r.first&&id<=r.last)return std::string(r.name)+" "+char('A'+id-r.first)+" SEAL";
    return "SEAL "+std::to_string(id);
}
