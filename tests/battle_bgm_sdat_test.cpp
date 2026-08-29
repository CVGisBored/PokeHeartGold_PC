#include "assets/sdat.hpp"
#include "game/battle_retail.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <set>
int main(int argc,char** argv){
    assert(argc>=2);
    HgSdat sdat;
    assert(sdat.load(std::filesystem::path(argv[1])/"data/sound/gs_sound_data.sdat"));
    std::set<std::uint16_t> ids(HG_RETAIL_BATTLE_BGM.begin(),HG_RETAIL_BATTLE_BGM.end());
    ids.insert(1125);ids.insert(1126);ids.insert(1127);
    for(auto id:ids){
        auto wave=sdat.renderSequence(id,0.35);
        if(!wave.valid||wave.samples.empty()){
            std::cerr<<"battle BGM sequence failed to render: "<<id<<"\n";
            return 2;
        }
    }
    std::cout<<"rendered "<<ids.size()<<" retail battle BGM sequences\n";
}
