#include "assets/nsbmd.hpp"
#include <array>
#include <cassert>
#include <filesystem>
#include <iostream>

int main(int argc,char** argv){
    assert(argc>=2);
    const auto narc=std::filesystem::path(argv[1]);
    // The three Johto starter follower resources each carry normal + shiny palettes.
    // Every directional/walk frame must default to palette 0 unless an authored 3D
    // material explicitly selects another palette.
    for(std::size_t member:std::array<std::size_t,3>{450,454,457}){
        auto m=load_nitro_texture_from_narc(narc,member);
        assert(m.valid);
        assert(m.paletteCount>=2);
        assert(m.textures.size()>=8);
        bool normalAndShinyDiffer=false;
        for(auto const& t:m.textures){
            assert(t.paletteVariants.size()>=2);
            assert(t.rgba==t.paletteVariants[0]);
            if(t.paletteVariants[0]!=t.paletteVariants[1])normalAndShinyDiffer=true;
        }
        assert(normalAndShinyDiffer);
    }
    std::cout<<"follower_palette_test: PASS\n";
}
