#include "assets/narc.hpp"
#include "assets/nsbmd.hpp"
#include "assets/nitro2d.hpp"
#include "game/new_game_assets.hpp"
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <iostream>

static std::uint32_t pixel(const NitroRgbaImage& im,int x,int y){
    assert(im.valid&&x>=0&&y>=0&&x<im.width&&y<im.height);
    const auto q=(std::size_t(y)*std::size_t(im.width)+std::size_t(x))*4;
    return (std::uint32_t(im.rgba[q])<<24)|(std::uint32_t(im.rgba[q+1])<<16)|
           (std::uint32_t(im.rgba[q+2])<<8)|std::uint32_t(im.rgba[q+3]);
}

int main(int argc,char** argv){
    assert(argc>=2);
    const auto arc=std::filesystem::path(argv[1]);
    auto boy=decode_nitro_char_sheet(read_narc_member(arc,hg_new_game_asset::BoyPoseGfx),
                                     read_narc_member(arc,hg_new_game_asset::BoyPalette),true,0);
    auto girl=decode_nitro_char_sheet(read_narc_member(arc,hg_new_game_asset::GirlPoseGfx),
                                      read_narc_member(arc,hg_new_game_asset::GirlPalette),true,0);
    assert(boy.valid&&boy.width==64&&boy.height==128);
    assert(girl.valid&&girl.width==64&&girl.height==128);
    // Stable authored pixels from the US HeartGold intro archive. These catch the
    // v0.23 cross-palette regression (boy<-Oak palette, girl<-boy palette).
    assert(pixel(boy,20,55)==0xFF734AFFu);  // Ethan jacket highlight
    assert(pixel(boy,32,50)==0x394252FFu);  // Ethan shorts/shadow
    assert(pixel(girl,20,55)==0xFF6B63FFu); // Lyra red top
    assert(pixel(girl,30,70)==0x426B94FFu); // Lyra blue overalls

    auto boyCells=decode_nitro_cells(read_narc_member(arc,hg_new_game_asset::BoyCellGfx),
                                     read_narc_member(arc,hg_new_game_asset::BoyCell),
                                     read_narc_member(arc,hg_new_game_asset::BoyPalette),true);
    auto girlCells=decode_nitro_cells(read_narc_member(arc,hg_new_game_asset::GirlCellGfx),
                                      read_narc_member(arc,hg_new_game_asset::GirlCell),
                                      read_narc_member(arc,hg_new_game_asset::GirlPalette),true);
    assert(boyCells.size()==1&&boyCells.front().valid);
    assert(girlCells.size()==1&&girlCells.front().valid);
    std::cout<<"new_game_asset_test: PASS\n";
}
