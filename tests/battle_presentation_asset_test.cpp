#include "assets/nitro2d.hpp"
#include "assets/nsbmd.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <vector>

static std::vector<unsigned char> member(const std::filesystem::path& narc,std::size_t i){
    auto raw=read_narc_member(narc,i);
    auto dec=nitro_lz10_decompress(raw);
    return dec.empty()?raw:dec;
}
static bool hasOpaque(const NitroRgbaImage& im){
    if(!im.valid)return false;
    for(std::size_t i=3;i<im.rgba.size();i+=4)if(im.rgba[i])return true;
    return false;
}
int main(int argc,char**argv){
    assert(argc>=2);std::filesystem::path root=argv[1];
    auto backs=root/"a/0/0/6";
    auto boy=decode_nitro_cells(member(backs,0),member(backs,2),member(backs,1),true);
    auto girl=decode_nitro_cells(member(backs,5),member(backs,7),member(backs,6),true);
    assert(!boy.empty()&&!girl.empty());assert(hasOpaque(boy.front())&&hasOpaque(girl.front()));

    auto hud=root/"a/0/0/8";
    auto enemy=decode_nitro_cells(member(hud,188),member(hud,187),member(hud,71),true);
    auto player=decode_nitro_cells(member(hud,191),member(hud,190),member(hud,71),true);
    assert(enemy.size()==1&&player.size()==1);assert(hasOpaque(enemy.front())&&hasOpaque(player.front()));

    auto bg=root/"a/0/0/7";int valid=0;
    for(int i=0;i<23;i++){
        auto im=decode_nitro_char_sheet(member(bg,3+i),member(bg,175+i),false,0);
        if(im.valid&&im.width>=256&&im.height>=192)valid++;
    }
    assert(valid==23);
    // Retail BattleInput resources: char 28, palette 246, main screens
    // 43/36/41, and fight screen 37. These IDs come directly from the game.
    for(int screen:{43,36,41,37}){
        auto im=decode_nitro_bg(member(bg,28),member(bg,screen),member(bg,246),true);
        assert(im.valid&&im.width==256&&im.height==256&&hasOpaque(im));
    }

    auto fronts=root/"a/0/5/8";
    auto class0=decode_nitro_cells(member(fronts,0),member(fronts,2),member(fronts,1),true);
    assert(!class0.empty()&&hasOpaque(class0.front()));
    std::cout<<"Retail battle presentation assets: Ethan="<<boy.size()<<" Lyra="<<girl.size()
             <<" HP HUD=2/2 backdrops="<<valid<<"/23 touch-menu=OK trainer-front=OK\n";
}
