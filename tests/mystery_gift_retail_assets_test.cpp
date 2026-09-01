#include "assets/narc.hpp"
#include "assets/nsbmd.hpp"
#include "assets/nitro2d.hpp"
#include "assets/sdat.hpp"
#include <filesystem>
#include <iostream>

static std::vector<unsigned char> member(const std::filesystem::path& arc,std::size_t i){
    auto raw=read_narc_member(arc,i);
    auto dec=nitro_lz10_decompress(raw);
    return dec.empty()?raw:dec;
}

int main(int argc,char** argv){
    if(argc<2){std::cerr<<"usage: mystery_gift_retail_assets_test <nitrofs>\n";return 1;}
    const std::filesystem::path fs=argv[1];
    const auto arc=fs/"a/0/8/8";
    auto info=inspect_narc(arc);
    if(!info.valid||info.members.size()<48){std::cerr<<"retail Mystery Gift NARC a/0/8/8 missing/short\n";return 2;}
    auto top=decode_nitro_bg(member(arc,11),member(arc,12),member(arc,3),false);
    auto menu=decode_nitro_bg(member(arc,19),member(arc,24),member(arc,20),false);
    auto receive=decode_nitro_bg(member(arc,2),member(arc,6),member(arc,3),false);
    auto pulse=decode_nitro_bg(member(arc,2),member(arc,8),member(arc,3),true);
    auto cells=decode_nitro_cells(member(arc,9),member(arc,1),member(arc,10),true);
    auto nanr=decode_nitro_nanr(member(arc,0));
    if(!top.valid||!menu.valid||!receive.valid||!pulse.valid||cells.size()<9||!nanr.valid||nanr.sequences.size()<5){
        std::cerr<<"failed to decode retail Mystery Gift UI/OAM/NANR resources\n";return 3;
    }
    for(double t: {0.0,0.10,0.20,0.35,0.55}){
        auto ci=sample_nitro_nanr_cell(nanr,2,t,60.0);
        if(ci>=cells.size()){std::cerr<<"retail Mystery Gift NANR sampled invalid cell\n";return 4;}
    }
    HgSdat sdat;
    if(!sdat.load(fs/"data/sound/gs_sound_data.sdat")){std::cerr<<"failed to load HG SDAT\n";return 5;}
    constexpr std::size_t kRetailMysteryGiftSeq=1149; // 0x47D, overlay 75
    auto seq=sdat.sequence(kRetailMysteryGiftSeq);
    auto wave=sdat.renderSequence(kRetailMysteryGiftSeq,0.40);
    if(!seq.valid||!wave.valid||wave.samples.empty()){
        std::cerr<<"retail Mystery Gift BGM 1149 failed to render\n";return 6;
    }
    std::cout<<"PASS: retail Mystery Gift a/0/8/8 UI + 9 OAM cells + 5 NANR sequences + BGM 1149 decoded\n";
    return 0;
}
