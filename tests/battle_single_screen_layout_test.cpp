#include "game/battle_layout.hpp"
#include <cassert>
#include <iostream>

static bool inside(const HgBattleUiRect& r){
    return r.x>=0&&r.y>=0&&r.x+r.w<=1280&&r.y+r.h<=720&&r.w>0&&r.h>0;
}
static bool overlap(const HgBattleUiRect&a,const HgBattleUiRect&b){
    return a.x<b.x+b.w&&a.x+a.w>b.x&&a.y<b.y+b.h&&a.y+a.h>b.y;
}
int main(){
    for(auto const&r:HG_BATTLE_MAIN_RECTS){assert(inside(r));assert(r.y>=535);}
    for(auto const&r:HG_BATTLE_MOVE_RECTS){assert(inside(r));assert(r.y>=535);}
    assert(inside(HG_BATTLE_CANCEL_RECT));assert(HG_BATTLE_CANCEL_RECT.y>=535);
    for(std::size_t i=0;i<HG_BATTLE_MAIN_RECTS.size();++i)for(std::size_t j=i+1;j<HG_BATTLE_MAIN_RECTS.size();++j)assert(!overlap(HG_BATTLE_MAIN_RECTS[i],HG_BATTLE_MAIN_RECTS[j]));
    // Move controls may sit alongside the compact BACK control but never overlap it.
    for(auto const&r:HG_BATTLE_MOVE_RECTS)assert(!overlap(r,HG_BATTLE_CANCEL_RECT));
    std::cout<<"Single-screen battle controls fit 1280x720 and stay in one lower panel.\n";
    return 0;
}
