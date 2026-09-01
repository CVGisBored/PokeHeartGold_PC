#include "game/mystery_gift.hpp"
#include <cstdlib>
#include <iostream>

int main(int argc,char** argv){
    HgMysteryGiftConfig cfg;cfg.host=argc>1?argv[1]:"127.0.0.1";cfg.port=argc>2?static_cast<std::uint16_t>(std::stoi(argv[2])):1249;cfg.timeoutMs=2000;
    const std::string client=argc>3?argv[3]:"integration-test-client";
    auto first=hg_fetch_mystery_gift(cfg,client,"TESTER");
    if(!first.transportOk||!first.hasGift||first.gift.type!=HgMysteryGiftType::Item||first.gift.itemId!=4||first.gift.quantity!=20){
        std::cerr<<"fetch failed: status="<<first.status<<" message="<<first.message<<"\n";return 2;
    }
    HgGameState state;std::string description;
    if(!hg_apply_mystery_gift(state,first.gift,&description)||!state.hasItem(4,20)||!state.mysteryGiftClaims.count(first.gift.id)){
        std::cerr<<"apply failed: "<<description<<"\n";return 3;
    }
    if(hg_apply_mystery_gift(state,first.gift,&description)){
        std::cerr<<"local duplicate claim was accepted\n";return 4;
    }
    // Before ACK, prove the save's received-ID list alone prevents a server
    // reissue. A different client ID simulates deleted client metadata / reset
    // server-side claim bookkeeping while preserving the same game save.
    auto saveBound=hg_fetch_mystery_gift(cfg,client+"-fresh-server-record","TESTER",state.mysteryGiftClaims);
    if(!saveBound.transportOk||!saveBound.alreadyClaimed){std::cerr<<"save-bound duplicate claim was not rejected: "<<saveBound.status<<" "<<saveBound.message<<"\n";return 5;}
    std::string ackError;if(!hg_ack_mystery_gift(cfg,client,first.gift.id,&ackError)){std::cerr<<"ack failed: "<<ackError<<"\n";return 6;}
    auto second=hg_fetch_mystery_gift(cfg,client,"TESTER",state.mysteryGiftClaims);
    if(!second.transportOk||!second.alreadyClaimed){std::cerr<<"server duplicate claim was not rejected: "<<second.status<<" "<<second.message<<"\n";return 7;}
    std::cout<<"PASS: received POKE BALL x20; save-bound duplicate rejected before ACK; server duplicate rejected after ACK\n";return 0;
}
