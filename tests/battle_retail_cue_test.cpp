#include "game/battle_retail.hpp"
#include <cassert>
#include <iostream>
int main(){
    auto wildJ=hg_retail_battle_cue(false,0,19,false);
    assert(wildJ.effect==42 && wildJ.transition==0xffff && wildJ.bgm==1116);
    auto wildK=hg_retail_battle_cue(false,0,19,true);
    assert(wildK.bgm==1125);
    auto trainerJ=hg_retail_battle_cue(true,1,19,false);
    assert(trainerJ.effect==41 && trainerJ.bgm==1117);
    auto trainerK=hg_retail_battle_cue(true,1,19,true);
    assert(trainerK.bgm==1126);
    auto gymJ=hg_retail_battle_cue(true,66,19,false);
    assert(gymJ.effect==0 && gymJ.transition==12 && gymJ.bgm==1118);
    auto gymK=hg_retail_battle_cue(true,66,19,true);
    assert(gymK.bgm==1127);
    auto rival=hg_retail_battle_cue(true,23,19,false);
    assert(rival.effect==21 && rival.transition==28 && rival.bgm==1119);
    auto rocket=hg_retail_battle_cue(true,55,19,false);
    assert(rocket.effect==29 && rocket.transition==39 && rocket.bgm==1120);
    auto raikou=hg_retail_battle_cue(false,0,243,false);
    assert(raikou.effect==22 && raikou.bgm==1123);
    auto hooh=hg_retail_battle_cue(false,0,250,false);
    assert(hooh.effect==25 && hooh.transition==35 && hooh.bgm==1132);
    auto lugia=hg_retail_battle_cue(false,0,249,false);
    assert(lugia.effect==26 && lugia.transition==36 && lugia.bgm==1133);
    std::cout << "retail battle cue table OK\n";
}
