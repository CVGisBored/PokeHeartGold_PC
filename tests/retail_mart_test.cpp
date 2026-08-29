#include "game/retail_mart.hpp"
#include <cassert>
#include <iostream>

int main(){
    auto b0=hg_standard_mart_inventory(0); assert(b0.size()==4); assert(b0[0].id==4); assert(b0.back().id==22);
    auto b8=hg_standard_mart_inventory(8); assert(b8.size()==19); assert(b8[2].id==2); assert(b8.back().id==77);
    auto s4=hg_special_mart_inventory(4); assert(s4.size()==12); assert(s4.front().id==4); assert(s4.back().id==145);
    auto s20=hg_special_mart_inventory(20); assert(s20.size()==12); assert(s20.front().id==348); assert(s20.back().id==406);
    auto d1=hg_decoration_mart_inventory(1); assert(d1.size()==6); assert(d1.front().id==115); assert(d1.front().price==100);
    auto seal=hg_seal_mart_inventory(6); assert(seal.size()==7); assert(seal.front().id==7); assert(seal.back().id==35);
    auto pre=hg_pokeathlon_daily_inventory(0,false); assert(pre.size()==6); assert(pre[4].id==221&&pre[4].price==3000);
    auto nat=hg_pokeathlon_daily_inventory(0,true); assert(nat.size()==12); assert(nat[8].id==80&&nat[11].id==109);
    auto cards0=hg_pokeathlon_data_card_inventory(0); assert(cards0.size()==6&&cards0.front().id==505&&cards0.back().id==510);
    auto cards4=hg_pokeathlon_data_card_inventory(26); assert(cards4.size()==3&&cards4.front().id==529&&cards4.back().price==9999);
    assert(hg_seal_name(49)=="SONG G SEAL");
    assert(hg_decoration_name(26)=="REFRIGERATOR");
    std::cout<<"retail mart tables: ok\n";
}
