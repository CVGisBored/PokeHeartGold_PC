#include "assets/land_data.hpp"
#include "assets/overworld_data.hpp"
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <iostream>

static const HgOverworldEvent* objectAt(const HgEventBank& bank,int x,int y){
    for(auto const& o:bank.overworlds)if(o.x==x&&o.y==y)return &o;
    return nullptr;
}

int main(int argc,char** argv){
    assert(argc>=2);
    const std::filesystem::path assets=argv[1];
    initialize_hg_map_headers(assets);

    auto verify=[&](int mapId,int playerX,int playerY,int counterX,int counterY,int clerkX,int clerkY,std::uint16_t model){
        auto const* h=hg_map_header(mapId);assert(h);
        auto matrix=load_hg_map_matrix(assets,h->matrixId);assert(matrix.valid&&!matrix.landMembers.empty());
        auto chunk=load_land_chunk(assets/"a/0/6/5",matrix.landMembers.front());assert(chunk.valid);
        auto const* counter=chunk.permission_at(counterX,counterY);assert(counter);
        assert(!counter->walkable());
        assert(hg_permission_is_counter(*counter));
        auto events=load_hg_event_bank(assets,h->eventsBank);assert(events.valid);
        auto const* clerk=objectAt(events,clerkX,clerkY);assert(clerk);
        assert(clerk->model==model);
        assert(clerk->scriptNumber!=0);
        // The service NPC is deliberately two tiles from the player, with the
        // behavior-0x80 counter occupying the intervening coordinate.
        const int manhattan=std::abs(clerkX-playerX)+std::abs(clerkY-playerY);
        assert(manhattan==2);
    };

    verify(68,4,6,3,6,2,6,334);  // Cherrygrove Poké Mart clerk
    verify(69,8,13,8,12,8,11,335); // Cherrygrove Pokémon Center nurse

    HgPermissionCell ordinary{0x00,0x80};
    assert(!hg_permission_is_counter(ordinary));

    // Directional ledge behaviors are the retail HG/SS values 56..59.
    assert(hg_permission_is_ledge_jump(HgPermissionCell{56,0x80}, 1, 0));
    assert(hg_permission_is_ledge_jump(HgPermissionCell{57,0x80},-1, 0));
    assert(hg_permission_is_ledge_jump(HgPermissionCell{58,0x80}, 0,-1));
    assert(hg_permission_is_ledge_jump(HgPermissionCell{59,0x80}, 0, 1));
    assert(!hg_permission_is_ledge_jump(HgPermissionCell{56,0x80},-1,0));
    assert(!hg_permission_is_ledge_jump(ordinary,0,1));
    std::cout<<"counter_tile_test: PASS\n";
}
