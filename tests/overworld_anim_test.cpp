#include "game/overworld_anim.hpp"
#include <array>
#include <cassert>
#include <iostream>
#include <set>

int main(){
    const std::array<int,4> bases={5,9,13,1}; // Down, Left, Right, Up
    for(int dir=0;dir<4;++dir){
        assert(hg_walk_base_frame(dir)==bases[std::size_t(dir)]);
        assert(hg_walk_frame_number(dir,false,0)==bases[std::size_t(dir)]);
        std::set<int> seen;
        for(int phase=0;phase<4;++phase){
            const int frame=hg_walk_frame_number(dir,true,phase);
            assert(frame==bases[std::size_t(dir)]+phase);
            seen.insert(frame);
        }
        assert(seen.size()==4); // both legs / complete directional cycle
        assert(hg_walk_frame_number(dir,true,4)==bases[std::size_t(dir)]);
    }
    assert(hg_walk_phase(0.000)==0);
    assert(hg_walk_phase(0.125)==1);
    assert(hg_walk_phase(0.250)==2);
    assert(hg_walk_phase(0.375)==3);
    assert(hg_walk_phase(0.500)==0);
    // Follower Pokemon use the retail compact eight-frame layout.
    assert(hg_follower_frame_number(3,false,0)==1);
    assert(hg_follower_frame_number(3,true,1)==10);
    assert(hg_follower_frame_number(0,false,0)==11);
    assert(hg_follower_frame_number(0,true,1)==12);
    assert(hg_follower_frame_number(1,false,0)==13);
    assert(hg_follower_frame_number(1,true,1)==14);
    assert(hg_follower_frame_number(2,false,0)==15);
    assert(hg_follower_frame_number(2,true,1)==16);
    std::cout << "overworld_anim_test: PASS\n";
}
