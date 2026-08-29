#include "assets/nitro2d.hpp"
#include "assets/narc.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <vector>
static std::vector<unsigned char> readMember(const std::filesystem::path& p,std::size_t idx){
    auto info=inspect_narc(p);assert(info.valid&&idx<info.members.size());std::ifstream f(p,std::ios::binary);f.seekg(0,std::ios::end);auto n=f.tellg();f.seekg(0);std::vector<unsigned char>b((std::size_t)n);f.read((char*)b.data(),n);
    std::size_t q=16,img=0;for(unsigned i=0;i<info.blockCount;i++){unsigned sz=b[q+4]|b[q+5]<<8|b[q+6]<<16|b[q+7]<<24;if(b[q]=='G'&&b[q+1]=='M'&&b[q+2]=='I'&&b[q+3]=='F')img=q;q+=sz;}auto &m=info.members[idx];return {b.begin()+img+8+m.offset,b.begin()+img+8+m.offset+m.size};
}
int main(int argc,char**argv){assert(argc>1);auto raw=readMember(argv[1],3);auto a=decode_nitro_nanr(raw);assert(a.valid);assert(a.sequences.size()>=2);assert(!a.sequences[1].frames.empty());assert(a.sequences[1].playbackMode==1);std::uint64_t duration=0;for(auto const& fr:a.sequences[1].frames)duration+=fr.duration;assert(duration==94);auto c0=sample_nitro_nanr_cell(a,1,0.0);auto c1=sample_nitro_nanr_cell(a,1,0.6);assert(c0<a.sequences[1].frames.size()+8);assert(c1<64);return 0;}
