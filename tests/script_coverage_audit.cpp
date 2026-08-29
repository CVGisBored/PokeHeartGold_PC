#include "assets/overworld_data.hpp"
#include "game/hg_script.hpp"
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <tuple>
#include <vector>

static void neutralizeYield(HgScriptVm& vm,const HgScriptYield& y){
    if(y.type==HgScriptYield::Type::WaitFrames&&y.b>=0x4000)vm.writeVar(y.b,0);
    if(y.type==HgScriptYield::Type::WaitInput&&y.a>=0x4000)vm.writeVar(y.a,1);
    if(y.type==HgScriptYield::Type::Choice&&y.a>=0x4000)vm.writeVar(y.a,0);
    if(y.type==HgScriptYield::Type::PositionQuery){if(y.a>=0x4000)vm.writeVar(y.a,0);if(y.b>=0x4000)vm.writeVar(y.b,0);if(y.c>=0x4000)vm.writeVar(y.c,0);}
}

static bool resolve(const std::vector<HgGlobalScriptEntry>& table,std::uint16_t id,HgGlobalScriptResolution& r){
    for(auto const&e:table){if(id>=e.minScriptId){r={true,id,e.scriptBank,e.messageBank,std::uint16_t(id-e.minScriptId+1)};return true;}}
    return false;
}

static std::size_t entryCount(const std::vector<unsigned char>& b){
    HgScriptVm vm;if(!vm.start(b,1))return 0;return vm.entryCount();
}

int main(int argc,char**argv){
    if(argc<2){std::cerr<<"usage: script_coverage_audit <nitrofs>\n";return 2;}
    std::filesystem::path assets=argv[1];auto table=load_hg_global_script_table(assets);if(table.empty())return 3;
    std::map<std::uint16_t,std::size_t> unsupported,errors,apps,objectOps;
    std::size_t scripts=0,finished=0,loops=0,globalTransfers=0;
    for(auto const&e:table){
        auto root=load_hg_script_bank(assets,e.scriptBank);const auto count=entryCount(root);if(!count)continue;
        for(std::size_t local=1;local<=count;local++){
            const std::uint32_t gid32=std::uint32_t(e.minScriptId)+std::uint32_t(local)-1;if(gid32>0xffff)break;
            HgGameState st;HgScriptVm vm;vm.bindState(&st);if(!vm.start(root,std::uint16_t(local),e.messageBank))continue;scripts++;
            bool done=false;
            for(int yields=0;yields<2048&&vm.active();yields++){
                auto y=vm.runUntilYield(nullptr,8192);
                if(y.type==HgScriptYield::Type::CommonScript){HgGlobalScriptResolution r;if(!resolve(table,y.a,r)){errors[0xfffe]++;break;}auto b=load_hg_script_bank(assets,r.scriptBank);if(!vm.enterExternalBank(b,r.localScriptNumber,r.messageBank,y.flag)){errors[0xfffd]++;break;}globalTransfers++;continue;}
                if(y.type==HgScriptYield::Type::Unsupported){unsupported[y.opcode]++;done=true;break;}
                if(y.type==HgScriptYield::Type::Error){errors[y.opcode]++;done=true;break;}
                if(y.type==HgScriptYield::Type::Finished){finished++;done=true;break;}
                if(y.type==HgScriptYield::Type::AppCommand)apps[y.opcode]++;
                if(y.type==HgScriptYield::Type::ObjectCommand)objectOps[y.opcode]++;
                neutralizeYield(vm,y);
            }
            if(!done&&vm.active())loops++;
        }
    }
    std::cout<<"standard scripts="<<scripts<<" finished="<<finished<<" loops="<<loops<<" globalTransfers="<<globalTransfers<<"\n";
    std::cout<<"unsupported:";for(auto [op,n]:unsupported)std::cout<<" "<<op<<":"<<n;std::cout<<"\nerrors:";for(auto [op,n]:errors)std::cout<<" "<<op<<":"<<n;std::cout<<"\napps:";for(auto [op,n]:apps)std::cout<<" "<<op<<":"<<n;std::cout<<"\nobject:";for(auto [op,n]:objectOps)std::cout<<" "<<op<<":"<<n;std::cout<<"\n";
    return unsupported.empty()&&errors.empty()?0:1;
}
