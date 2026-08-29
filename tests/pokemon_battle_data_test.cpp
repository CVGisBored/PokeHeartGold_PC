#include "assets/pokemon_data.hpp"
#include "assets/land_data.hpp"
#include "game/hg_state.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
int main(int argc,char** argv){
    assert(argc>1);std::filesystem::path a=argv[1];assert(hg_initialize_pokemon_database(a));
    assert(hg_species_count()==493);assert(hg_move_count()==467);
    auto* bulba=hg_personal_data(1);auto* arceus=hg_personal_data(493);auto* tackle=hg_move_data(33);auto* ember=hg_move_data(52);
    assert(bulba&&bulba->hp==45&&bulba->attack==49&&bulba->type1==12&&bulba->type2==3);
    assert(arceus&&arceus->hp==120);assert(hg_rom_species_name(493)=="ARCEUS");
    assert(tackle&&tackle->power==35&&tackle->accuracy==95&&tackle->pp==35&&tackle->category==0);
    assert(ember&&ember->power==40&&ember->type==10&&ember->category==1&&ember->effectChance==10);
    auto c=hg_make_mon(152,5);assert(c.maxHp>1&&c.attack>1&&c.moves[0]!=0&&c.maxPp[0]>0);assert(c.exp==hg_exp_for_level(152,5));assert(hg_level_for_exp(152,c.exp)==5);assert(hg_exp_for_level(152,6)>c.exp);
    auto route=load_land_chunk(a/"a/0/6/5",1);assert(route.valid&&route.permissions.size()==1024);int grass=0,plain=0;for(auto const& p:route.permissions){if(hg_permission_allows_land_encounter(p))grass++;else if(p.walkable()&&p.type==0)plain++;}assert(grass==124&&plain>0);
    HgPermissionCell ordinary{0,0};assert(!hg_permission_allows_land_encounter(ordinary));HgPermissionCell tall{2,0};assert(hg_permission_allows_land_encounter(tall));HgPermissionCell cave{8,0};assert(hg_permission_allows_land_encounter(cave));
    std::cout<<"ROM Pokemon database: 493 species / 467 moves; Route 29 encounter grass cells="<<grass<<"\n";return 0;
}
