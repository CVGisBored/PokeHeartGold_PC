#include "assets/overworld_data.hpp"
#include "assets/narc.hpp"
#include "game/script_header.hpp"
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <set>
#include <vector>

static std::uint16_t u16(const std::vector<unsigned char>& b,std::size_t p){return std::uint16_t(b[p])|(std::uint16_t(b[p+1])<<8);}
static std::uint32_t u32(const std::vector<unsigned char>& b,std::size_t p){return std::uint32_t(b[p])|(std::uint32_t(b[p+1])<<8)|(std::uint32_t(b[p+2])<<16)|(std::uint32_t(b[p+3])<<24);}
static std::size_t script_entry_count(const std::vector<unsigned char>& b){
    std::size_t n=0;
    for(std::size_t p=0;p+2<=b.size();p+=4){
        if(u16(b,p)==0xfd13) return n;
        if(p+4>b.size()) return 0;
        auto rel=static_cast<std::int32_t>(u32(b,p));
        auto dest=static_cast<std::int64_t>(p)+4+rel;
        if(dest<0||dest>=static_cast<std::int64_t>(b.size())) return 0;
        if(++n>4096) return 0;
    }
    return 0;
}

int main(int argc,char** argv){
    if(argc<2){std::cerr<<"usage: script_header_audit <nitrofs>\n";return 2;}
    std::filesystem::path assets=argv[1];
    if(!initialize_hg_map_headers(assets)||!hg_map_headers_from_rom()){
        std::cerr<<"retail map headers unavailable\n";return 1;
    }
    std::size_t headers=0,valid=0,frameRows=0,initRefs=0,fail=0;
    std::set<std::uint16_t> uniqueHeaderMembers;
    for(auto const& mh:hg_supported_map_headers()){
        ++headers;uniqueHeaderMembers.insert(mh.scriptHeaderBank);
        auto raw=read_narc_member(assets/"a/0/1/2",mh.scriptHeaderBank);
        auto sh=parse_hg_script_header(raw);
        if(!sh.valid){std::cerr<<"map "<<mh.mapId<<" header member "<<mh.scriptHeaderBank<<" did not parse\n";++fail;continue;}
        ++valid;frameRows+=sh.frame.size();
        auto script=load_hg_script_bank(assets,mh.scriptsBank);
        auto count=script_entry_count(script);
        if(count==0){std::cerr<<"map "<<mh.mapId<<" script member "<<mh.scriptsBank<<" has no valid entry table\n";++fail;continue;}
        auto check=[&](std::uint16_t id,const char* kind){
            if(!id)return;
            ++initRefs;
            if(id>=2000){
                auto r=resolve_hg_global_script(assets,id);
                if(!r.valid){std::cerr<<"map "<<mh.mapId<<" "<<kind<<" global script "<<id<<" did not resolve\n";++fail;return;}
                auto gb=load_hg_script_bank(assets,r.scriptBank);
                auto gc=script_entry_count(gb);
                if(!gc||r.localScriptNumber>gc){std::cerr<<"map "<<mh.mapId<<" "<<kind<<" global script "<<id<<" local "<<r.localScriptNumber<<" > entries "<<gc<<"\n";++fail;}
                return;
            }
            if(id>count){std::cerr<<"map "<<mh.mapId<<" "<<kind<<" script "<<id<<" > entries "<<count<<"\n";++fail;}
        };
        check(sh.transition,"transition");check(sh.resume,"resume");check(sh.load,"load");
        for(auto const& f:sh.frame)check(f.script,"frame");
    }
    std::cout<<"script headers="<<headers<<" valid="<<valid<<" uniqueMembers="<<uniqueHeaderMembers.size()
             <<" frameRows="<<frameRows<<" referencedScripts="<<initRefs<<" failures="<<fail<<"\n";
    return fail?1:0;
}
