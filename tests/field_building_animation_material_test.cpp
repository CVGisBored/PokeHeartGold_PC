#include "assets/nsbmd.hpp"
#include "assets/land_data.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>

static float luminance(Color c){return (c.r+c.g+c.b)/3.0f;}

int main(int argc,char** argv){
    if(argc<2){std::cerr<<"usage: field_building_animation_material_test <nitrofs>\n";return 2;}
    const std::filesystem::path root=argv[1];
    const auto fieldArc=root/"fielddata/build_model/bm_field.narc";
    const auto roomArc=root/"fielddata/build_model/bm_room.narc";
    const auto animArc=root/"a/1/0/6";
    const auto fieldList=root/"a/1/0/7";

    HgPermissionCell stairEast{};stairEast.type=94;
    HgPermissionCell stairWest{};stairWest.type=95;
    if(hg_permission_stair_warp_direction(stairEast)!=1||hg_permission_stair_warp_direction(stairWest)!=-1){
        std::cerr<<"retail stair-warp direction decode failed\n";return 1;
    }

    auto wind=load_nsbmd_from_narc(fieldArc,28);
    if(!wind.valid||wind.models.empty()||wind.textures.empty()){
        std::cerr<<"wind build model 28 failed to decode\n";return 1;
    }
    auto srt=load_build_model_nsbta(fieldList,animArc,28);
    if(!srt.valid||srt.frameCount!=120||srt.tracks.size()!=3){
        std::cerr<<"wind NSBTA missing/wrong: "<<srt.error<<" frames="<<srt.frameCount<<" tracks="<<srt.tracks.size()<<"\n";return 1;
    }
    std::set<std::string> targets;for(auto const&t:srt.tracks)targets.insert(t.materialName);
    for(auto const* name:{"wind_lm3","wind_lm4","wind_lm5"})if(!targets.count(name)){
        std::cerr<<"wind NSBTA target missing: "<<name<<"\n";return 1;
    }
    for(auto const& name:wind.models.front().materialNames)if(!targets.count(name)){
        std::cerr<<"wind model material is not animated: "<<name<<"\n";return 1;
    }
    const auto first=sample_nsbta_uv(srt,"wind_lm3",0.0);
    const auto later=sample_nsbta_uv(srt,"wind_lm3",0.5);
    if(std::fabs(first.x-later.x)<0.01f&&std::fabs(first.y-later.y)<0.01f){
        std::cerr<<"wind material animation does not advance\n";return 1;
    }

    // New Bark's wind generator is bm_field 27 (wk_sp1).  v0.46 had this
    // working; the v0.46.1 door-loop hotfix accidentally stopped its continuous
    // BCA and restored the wrong joint-scale mode for this model.  Confirm the
    // authored 240-frame rotor stays in the separated Nitro scale space.
    auto newBarkTurbine=load_nsbmd_from_narc(fieldArc,27);
    auto newBarkJnt=load_build_model_nsbca(fieldList,animArc,27);
    if(!newBarkTurbine.valid||newBarkTurbine.models.empty()||newBarkTurbine.models.front().name!="wk_sp1"||
       !newBarkJnt.valid||newBarkJnt.frameCount!=240||newBarkJnt.tracks.size()!=3){
        std::cerr<<"New Bark turbine rotor/joint animation missing: "<<newBarkJnt.error<<"\n";return 1;
    }
    auto const& nbm=newBarkTurbine.models.front();
    if(std::fabs(nbm.normalizedScale-(1.0f/64.0f))>0.0001f||nbm.jointBindWorldMatrices.size()<3){
        std::cerr<<"New Bark turbine Nitro scale/joints wrong\n";return 1;
    }
    const NsbmdTriangle* nbRotor=nullptr;
    for(auto const& tri:nbm.triangles)if(tri.jointIndex>0){nbRotor=&tri;break;}
    if(!nbRotor){std::cerr<<"New Bark turbine rotor triangles lost joint binding\n";return 1;}
    auto nb0=transform_nsbmd_point(sample_nsbca_joint_delta(nbm,newBarkJnt,nbRotor->jointIndex,0.0,30.0),nbRotor->a.position);
    auto nb1=transform_nsbmd_point(sample_nsbca_joint_delta(nbm,newBarkJnt,nbRotor->jointIndex,1.0,30.0),nbRotor->a.position);
    if(std::fabs(nb0.x-nb1.x)+std::fabs(nb0.y-nb1.y)+std::fabs(nb0.z-nb1.z)<0.01f){
        std::cerr<<"New Bark turbine rotor joint animation does not advance\n";return 1;
    }

    // Route 14/13 uses the related a13_anemo model 291.
    auto turbine=load_nsbmd_from_narc(fieldArc,291);
    auto turbineJnt=load_build_model_nsbca(fieldList,animArc,291);
    if(!turbine.valid||turbine.models.empty()||turbine.models.front().name!="a13_anemo"||
       !turbineJnt.valid||turbineJnt.frameCount!=240||turbineJnt.tracks.size()!=3){
        std::cerr<<"retail turbine rotor/joint animation missing: "<<turbineJnt.error<<"\n";return 1;
    }
    auto const& tm=turbine.models.front();
    if(std::fabs(tm.normalizedScale-(1.0f/64.0f))>0.0001f||tm.jointBindWorldMatrices.size()<3){
        std::cerr<<"turbine Nitro scale/joints wrong\n";return 1;
    }
    const NsbmdTriangle* rotorTri=nullptr;
    for(auto const& tri:tm.triangles)if(tri.jointIndex>0){rotorTri=&tri;break;}
    if(!rotorTri){std::cerr<<"turbine rotor triangles lost joint binding\n";return 1;}
    auto p0=transform_nsbmd_point(sample_nsbca_joint_delta(tm,turbineJnt,rotorTri->jointIndex,0.0,30.0),rotorTri->a.position);
    auto p1=transform_nsbmd_point(sample_nsbca_joint_delta(tm,turbineJnt,rotorTri->jointIndex,1.0,30.0),rotorTri->a.position);
    const float rotorMotion=std::fabs(p0.x-p1.x)+std::fabs(p0.y-p1.y)+std::fabs(p0.z-p1.z);
    if(rotorMotion<0.01f){std::cerr<<"turbine rotor joint animation does not advance\n";return 1;}

    // Standalone stair build models also use the same texture-color material
    // convention. They must never regress to pure black.
    for(int id:{66,67,69,70}){
        auto stair=load_nsbmd_from_narc(fieldArc,std::size_t(id));
        if(!stair.valid||stair.models.empty()){std::cerr<<"stair model "<<id<<" failed to decode\n";return 1;}
        for(auto const& tri:stair.models.front().triangles){
            if(tri.textureIndex<0)continue;
            if(luminance(tri.rasterBaseColor)<0.05f){std::cerr<<"stair model "<<id<<" still renders black\n";return 1;}
        }
    }

    // These retail room members cover the material convention that previously
    // rendered valid furniture/plant textures as pure black.
    for(int id:{23,24,53,64,89}){
        auto room=load_nsbmd_from_narc(roomArc,std::size_t(id));
        if(!room.valid||room.models.empty()){
            std::cerr<<"room member "<<id<<" failed to decode\n";return 1;
        }
        std::size_t textured=0,black=0;double average=0.0;
        for(auto const& tri:room.models.front().triangles){
            if(tri.textureIndex<0)continue;
            ++textured;const float l=luminance(tri.rasterBaseColor);average+=l;if(l<0.05f)++black;
        }
        if(textured==0||black!=0||average/double(textured)<0.20){
            std::cerr<<"room member "<<id<<" still has black textured furniture: textured="<<textured<<" black="<<black<<" avg="<<(textured?average/double(textured):0.0)<<"\n";return 1;
        }
    }
    std::cout<<"PASS: retail wind NSBTA + New Bark/Route turbine BCA rotors + stair/interior texture-color materials\n";
    return 0;
}
