#include "game/mystery_gift.hpp"
#include <iostream>

int main(int argc,char** argv){
    HgMysteryGiftConfig cfg;cfg.host=argc>1?argv[1]:"127.0.0.1";cfg.port=argc>2?static_cast<std::uint16_t>(std::stoi(argv[2])):1249;cfg.timeoutMs=2000;
    const std::string client=argc>3?argv[3]:"pokemon-integration-test";
    auto r=hg_fetch_mystery_gift(cfg,client,"TESTER");
    if(!r.transportOk||!r.hasGift||r.gift.type!=HgMysteryGiftType::Pokemon){std::cerr<<"pokemon fetch failed: "<<r.status<<" "<<r.message<<"\n";return 2;}
    auto const& m=r.gift.pokemon;
    if(m.species!=25||m.level!=50||m.moves[0]!=85||m.moves[1]!=98||m.moves[2]!=86||m.moves[3]!=104||m.maxHp!=110||m.hp!=110||m.attack!=75||m.defense!=55||m.spAttack!=70||m.spDefense!=65||m.speed!=110){
        std::cerr<<"pokemon payload did not preserve configured moves/stats\n";return 3;
    }
    HgGameState state;std::string desc;if(!hg_apply_mystery_gift(state,r.gift,&desc)||state.party.size()!=1||!state.hasSpecies(25)){std::cerr<<"pokemon apply failed: "<<desc<<"\n";return 4;}
    std::string ackError;if(!hg_ack_mystery_gift(cfg,client,r.gift.id,&ackError)){std::cerr<<"pokemon ACK failed: "<<ackError<<"\n";return 5;}
    std::cout<<"PASS: custom Pokemon moves/stats preserved and Pokemon stored\n";return 0;
}
