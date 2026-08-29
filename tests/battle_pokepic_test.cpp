#include "assets/nitro2d.hpp"
#include "assets/nsbmd.hpp"
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <iostream>

static std::size_t opaquePixels(const NitroRgbaImage& im){
    std::size_t n=0;
    for(std::size_t i=3;i<im.rgba.size();i+=4)if(im.rgba[i]>=16)++n;
    return n;
}

int main(int argc,char** argv){
    assert(argc>=2);
    const std::filesystem::path arc=argv[1];
    // Retail pokegra uses species * 6. Chikorita (152), normal male:
    // back char = 152*6+1, front char = 152*6+3, normal palette = +4.
    constexpr std::size_t species=152,base=species*6;
    auto pal=read_narc_member(arc,base+4);
    auto back=decode_hg_pokepic(read_narc_member(arc,base+1),pal,true,0);
    auto front=decode_hg_pokepic(read_narc_member(arc,base+3),pal,true,0);
    auto backFrame1=decode_hg_pokepic(read_narc_member(arc,base+1),pal,true,1);
    assert(back.valid&&front.valid&&backFrame1.valid);
    assert(back.width==80&&back.height==80&&front.width==80&&front.height==80);
    auto bo=opaquePixels(back),fo=opaquePixels(front),b1=opaquePixels(backFrame1);
    assert(bo>150&&bo<6200);
    assert(fo>150&&fo<6200);
    assert(b1>150&&b1<6200);
    // The second authored frame should not be byte-identical to frame zero.
    assert(back.rgba!=backFrame1.rgba);
    std::cout<<"PASS: HG pokepic retail unscan decoded Chikorita front/back 80x80 frames"
             <<" opaque="<<fo<<"/"<<bo<<" frame1="<<b1<<"\n";
    return 0;
}
