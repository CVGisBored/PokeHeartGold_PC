#include "platform/runtime_paths.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

int main(int argc,char** argv){
    namespace fs=std::filesystem;
    std::error_code ec;
    auto root=fs::temp_directory_path()/"hg_runtime_path_test";
    fs::remove_all(root,ec);
    fs::create_directories(root/"bin",ec);
    fs::create_directories(root/"assets/nitrofs/a/0/0",ec);
    fs::create_directories(root/"assets/nitrofs/a/0/8",ec);
    fs::create_directories(root/"assets/nitrofs/data/sound",ec);
    std::ofstream(root/"assets/nitrofs/a/0/0/4").put('x');
    std::ofstream(root/"assets/nitrofs/a/0/8/1").put('x');
    std::ofstream(root/"assets/nitrofs/data/sound/gs_sound_data.sdat").put('x');
    auto fakeExe=(root/"bin/HeartGoldNative-v0.30-Windows-x86_64.exe").string();
    auto found=hgFindRetailNitroFs(fakeExe.c_str());
    assert(hgLooksLikeRetailNitroFs(found));
    assert(found.lexically_normal()==(root/"assets/nitrofs").lexically_normal());
    fs::remove_all(root,ec);

    // Packaging regression guard: towns/cities need both the placed-model NARCs
    // and their material/shape sidecar data. v0.38 accidentally omitted these
    // from the release archive, which left terrain intact but removed all 3D
    // buildings from outdoor maps.
    if(argc>1){
        fs::path nitro=argv[1];
        const fs::path required[]={
            nitro/"fielddata/build_model/bm_field.narc",
            nitro/"fielddata/build_model/bm_field_matshp.dat",
            nitro/"fielddata/build_model/bm_room.narc",
            nitro/"fielddata/build_model/bm_room_matshp.dat"
        };
        for(auto const& f:required){
            assert(fs::exists(f));
            assert(fs::is_regular_file(f));
            assert(fs::file_size(f)>0);
        }
    }
    std::cout<<"Executable-relative retail NitroFS discovery + building-model package assets: PASS\n";
    return 0;
}
