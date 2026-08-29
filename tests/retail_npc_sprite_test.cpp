#include "assets/overworld_data.hpp"
#include "assets/narc.hpp"
#include "game/overworld_sprite_map.hpp"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <set>
#include <unordered_map>

int main(int argc,char** argv){
    assert(argc>=2);
    const std::filesystem::path assets=argv[1];
    assert(initialize_hg_map_headers(assets));
    assert(hg_map_headers_from_rom());

    const auto& headers=hg_supported_map_headers();
    assert(headers.size()>=540);
    const auto narc=inspect_narc(assets/"a/0/8/1");
    assert(narc.valid);

    std::set<std::uint16_t> banks;
    std::set<std::uint16_t> spriteIds;
    std::size_t objects=0,dynamic=0,specialNonBmd=0,unresolved=0;
    for(auto const& h:headers)banks.insert(h.eventsBank);
    for(auto bankId:banks){
        auto bank=load_hg_event_bank(assets,bankId);
        assert(bank.valid);
        for(auto const& o:bank.overworlds){
            ++objects;spriteIds.insert(o.model);
            if(hgss::isDynamicSprite(o.model)){++dynamic;continue;}
            const int member=hgss::mmodelMemberForSprite(o.model);
            if(member<0){
                // The four gate sprites are field-layer/special graphics rather than
                // BMD/BTX entries in a/0/8/1.  They must never become generic NPCs.
                if(o.model==251||o.model==252||o.model==254||o.model==255){++specialNonBmd;continue;}
                std::cerr<<"Unresolved retail event sprite id "<<o.model<<" in event bank "<<bankId<<"\n";
                ++unresolved;continue;
            }
            if(std::size_t(member)>=narc.members.size()){
                std::cerr<<"Out-of-range mmodel member "<<member<<" for sprite "<<o.model<<"\n";
                ++unresolved;
            }
        }
    }

    assert(objects>2500);              // full Johto + Kanto event population
    assert(spriteIds.size()>225);      // broad retail graphics coverage
    assert(dynamic>0);                 // scripted graphics slots exist in retail maps
    assert(unresolved==0);

    // Dynamic object graphics are exact script-variable slots.
    std::unordered_map<std::uint16_t,std::uint16_t> vars;
    assert(!hgss::resolveDynamicSprite(101,vars).has_value());
    vars[0x4021]=97; // VAR_OBJ_1 -> SPRITE_HEROINE
    auto lyra=hgss::resolveDynamicSprite(101,vars);
    assert(lyra&&*lyra==97&&hgss::mmodelMemberForSprite(*lyra)==70);
    vars[0x4021]=0;  // VAR_OBJ_1 -> SPRITE_HERO
    auto ethan=hgss::resolveDynamicSprite(101,vars);
    assert(ethan&&*ethan==0&&hgss::mmodelMemberForSprite(*ethan)==69);

    std::cout<<"retail_npc_sprite_test: PASS maps="<<headers.size()
             <<" eventBanks="<<banks.size()<<" objects="<<objects
             <<" spriteIds="<<spriteIds.size()<<" dynamic="<<dynamic
             <<" specialNonBmd="<<specialNonBmd<<"\n";
}
