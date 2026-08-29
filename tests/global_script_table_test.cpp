#include "assets/overworld_data.hpp"
#include "game/hg_script.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <set>

int main(int argc,char** argv){
    if(argc<2){std::cerr<<"usage: global_script_table_test <nitrofs>\n";return 2;}
    const std::filesystem::path assets=argv[1];
    auto table=load_hg_global_script_table(assets);
    assert(table.size()==30);
    for(std::size_t i=1;i<table.size();++i)assert(table[i-1].minScriptId>table[i].minScriptId);
    auto check=[&](std::uint16_t id,std::uint16_t bank,std::uint16_t msg,std::uint16_t local){
        auto r=resolve_hg_global_script(assets,id);assert(r.valid);assert(r.scriptBank==bank);assert(r.messageBank==msg);assert(r.localScriptNumber==local);
        auto bytes=load_hg_script_bank(assets,r.scriptBank);HgScriptVm vm;assert(vm.start(bytes,r.localScriptNumber,r.messageBank));
        assert(vm.messageBankId()==r.messageBank);
    };
    check(2000,3,40,1);check(2008,3,40,9);check(2500,1,20,1);check(2800,150,23,1);
    check(3000,953,40,1);check(5000,953,40,1);check(7000,141,199,1);check(8000,145,210,1);
    check(10490,263,433,1);
    // Every dispatch range must point at a loadable bank with entry 1, except
    // the retail empty/special range member which is intentionally zero-entry.
    std::set<std::uint16_t> checked;
    for(auto const& e:table){
        if(!checked.insert(e.scriptBank).second)continue;
        auto bytes=load_hg_script_bank(assets,e.scriptBank);
        assert(!bytes.empty());
        HgScriptVm vm;
        if(e.scriptBank==734){assert(!vm.start(bytes,1,e.messageBank));continue;}
        assert(vm.start(bytes,1,e.messageBank));
    }
    std::cout<<"global_script_table_test: PASS ranges="<<table.size()<<"\n";
}
