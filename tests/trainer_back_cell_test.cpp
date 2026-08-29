#include "assets/narc.hpp"
#include "assets/nitro2d.hpp"
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <vector>

static std::vector<unsigned char> readMember(const std::filesystem::path& p,std::size_t idx){
    auto info=inspect_narc(p);assert(info.valid&&idx<info.members.size());
    std::ifstream f(p,std::ios::binary);f.seekg(0,std::ios::end);auto n=f.tellg();f.seekg(0);
    std::vector<unsigned char>b((std::size_t)n);f.read((char*)b.data(),n);
    std::size_t q=16,img=0;for(unsigned i=0;i<info.blockCount;i++){
        unsigned sz=b[q+4]|b[q+5]<<8|b[q+6]<<16|b[q+7]<<24;
        if(b[q]=='G'&&b[q+1]=='M'&&b[q+2]=='I'&&b[q+3]=='F')img=q;
        q+=sz;
    }
    auto &m=info.members[idx];return {b.begin()+img+8+m.offset,b.begin()+img+8+m.offset+m.size};
}
static std::uint64_t hashImage(const NitroRgbaImage& im){
    std::uint64_t h=1469598103934665603ull;
    for(auto v:im.rgba){h^=v;h*=1099511628211ull;}
    return h;
}
int main(int argc,char**argv){
    assert(argc>1);const std::filesystem::path arc=argv[1];
    auto ncgr=readMember(arc,0),nclr=readMember(arc,1),ncer=readMember(arc,2),nanr=readMember(arc,3);
    auto cells=decode_nitro_cells(ncgr,ncer,nclr,true);assert(cells.size()==8);
    std::set<std::uint64_t> hashes;for(auto const& c:cells){assert(c.valid);hashes.insert(hashImage(c));}
    // trbgra is a VRAM-transfer bank: all eight cells reuse the same OAM tile
    // names but point at different 3200-byte character slices. A decoder that
    // ignores the transfer table degenerates into the static-frame v0.37 bug.
    assert(hashes.size()==8);
    auto anim=decode_nitro_nanr(nanr);assert(anim.valid&&anim.sequences.size()>=2);
    auto const& throwSeq=anim.sequences[1];assert(throwSeq.playbackMode==1&&throwSeq.frames.size()==9);
    std::uint64_t duration=0;for(auto const& fr:throwSeq.frames)duration+=fr.duration;assert(duration==94);
    assert(sample_nitro_nanr_cell(anim,1,0.0)==0);
    assert(sample_nitro_nanr_cell(anim,1,40.0/60.0)==7);
    return 0;
}
