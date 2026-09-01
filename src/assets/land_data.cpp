#include "assets/land_data.hpp"
#include "assets/narc.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace {
std::uint16_t u16(const std::vector<unsigned char>& b, std::size_t p) {
    if (p + 2 > b.size()) return 0;
    return std::uint16_t(b[p]) | (std::uint16_t(b[p + 1]) << 8);
}
std::int16_t s16(const std::vector<unsigned char>& b, std::size_t p) {
    return static_cast<std::int16_t>(u16(b,p));
}
std::uint32_t u32(const std::vector<unsigned char>& b, std::size_t p) {
    if (p + 4 > b.size()) return 0;
    return std::uint32_t(b[p]) |
           (std::uint32_t(b[p + 1]) << 8) |
           (std::uint32_t(b[p + 2]) << 16) |
           (std::uint32_t(b[p + 3]) << 24);
}
std::int32_t s32(const std::vector<unsigned char>& b, std::size_t p) {
    return static_cast<std::int32_t>(u32(b,p));
}



int slope_type(int x,int z,int y) {
    static constexpr std::array<std::array<int,3>,6> known{{
        {{0,4096,0}}, {{0,4096,0}}, {{2896,2896,0}}, {{-2896,2896,0}}, {{0,2896,2896}}, {{0,2896,-2896}}
    }};
    for (std::size_t i=0;i<known.size();++i) if (known[i][0]==x && known[i][1]==z && known[i][2]==y) return static_cast<int>(i);
    return 6;
}

float decode_height(const std::vector<unsigned char>& b,std::size_t p) {
    const auto frac=u16(b,p);
    const auto whole=s16(b,p+2);
    return -static_cast<float>(whole) - static_cast<float>(frac)/65536.0f;
}

float plate_z(float d,int sx,int sz,int sy,int x0,int x1,int y0,int y1) {
    constexpr float slopeUnit=4095.56247663f;
    float xd=sx/slopeUnit, zd=sz/slopeUnit, yd=sy/slopeUnit;
    float px=d*xd, pz=d*zd, py=d*yd;
    constexpr float eps=0.001f;
    if (std::fabs(zd)<eps) return d;
    if (std::fabs(xd)>eps) {
        float m=-xd/zd;
        return std::min(m*(x0-px)+pz,m*(x1-px)+pz);
    }
    if (std::fabs(yd)>eps) {
        float m=-yd/zd;
        return std::min(m*(y0-py)+pz,m*(y1-py)+pz);
    }
    return d;
}
}

BdhcData parse_hgss_bdhc(const std::vector<unsigned char>& b) {
    BdhcData out;
    if (b.size()<16 || std::memcmp(b.data(),"BDHC",4)!=0) { out.error="not HGSS BDHC"; return out; }
    out.numCoords=u16(b,4); out.numSlopes=u16(b,6); out.numHeights=u16(b,8); out.numPlates=u16(b,10);
    constexpr std::size_t header=0x10, coordSize=8, slopeSize=12, heightSize=4, plateSize=8;
    std::size_t coordsOff=header;
    std::size_t slopesOff=coordsOff+coordSize*out.numCoords;
    std::size_t heightsOff=slopesOff+slopeSize*out.numSlopes;
    std::size_t platesOff=heightsOff+heightSize*out.numHeights;
    if (platesOff+plateSize*out.numPlates>b.size()) { out.error="BDHC tables exceed block size"; return out; }

    std::vector<int> xs(out.numCoords),ys(out.numCoords);
    for(std::size_t i=0;i<out.numCoords;i++){std::size_t p=coordsOff+i*coordSize;xs[i]=s16(b,p+2);ys[i]=s16(b,p+6);}
    struct Slope{int x,z,y;}; std::vector<Slope> slopes(out.numSlopes);
    for(std::size_t i=0;i<out.numSlopes;i++){std::size_t p=slopesOff+i*slopeSize;slopes[i]={s32(b,p),s32(b,p+4),s32(b,p+8)};}
    std::vector<float> heights(out.numHeights);
    for(std::size_t i=0;i<out.numHeights;i++) heights[i]=decode_height(b,heightsOff+i*heightSize);

    out.plates.reserve(out.numPlates);
    for(std::size_t i=0;i<out.numPlates;i++){
        std::size_t p=platesOff+i*plateSize;
        std::size_t a=u16(b,p), c=u16(b,p+2), si=u16(b,p+4), hi=u16(b,p+6);
        if(a>=xs.size()||c>=xs.size()||si>=slopes.size()||hi>=heights.size()){out.error="BDHC plate index out of range";out.plates.clear();return out;}
        auto s=slopes[si];
        BdhcPlate plate;plate.x=xs[a];plate.y=ys[a];plate.width=xs[c]-xs[a];plate.height=ys[c]-ys[a];
        plate.planeD=heights[hi];plate.z=plate_z(plate.planeD,s.x,s.z,s.y,xs[a],xs[c],ys[a],ys[c]);plate.slopeX=s.x;plate.slopeZ=s.z;plate.slopeY=s.y;plate.type=slope_type(s.x,s.z,s.y);
        out.plates.push_back(plate);
    }
    out.valid=true;
    return out;
}

bool sample_bdhc_height(const BdhcData& data, float x, float y, float& heightOut) {
    if(!data.valid) return false;
    bool found=false; float best=-std::numeric_limits<float>::infinity();
    constexpr float slopeUnit=4095.56247663f;
    for(auto const& p:data.plates){
        float x0=static_cast<float>(std::min(p.x,p.x+p.width));
        float x1=static_cast<float>(std::max(p.x,p.x+p.width));
        float y0=static_cast<float>(std::min(p.y,p.y+p.height));
        float y1=static_cast<float>(std::max(p.y,p.y+p.height));
        if(x<x0||x>x1||y<y0||y>y1) continue;
        float xd=p.slopeX/slopeUnit, zd=p.slopeZ/slopeUnit, yd=p.slopeY/slopeUnit;
        if(std::fabs(zd)<0.001f) continue;
        float h=(p.planeD-xd*x-yd*y)/zd;
        if(!found||h>best){best=h;found=true;}
    }
    if(found) heightOut=best;
    return found;
}

LandChunk load_land_chunk(const std::filesystem::path& landNarc, std::size_t memberIndex) {
    LandChunk out;
    out.memberIndex = memberIndex;
    auto raw = read_narc_member(landNarc, memberIndex);
    out.rawSize = raw.size();
    if (raw.size() < 20) { out.error = "map-file member missing/truncated"; return out; }

    // HGSS map-file layout: four section sizes, BGS header/data, permissions,
    // building placements, BMD0, then BDHC.  Parsing by the section table makes
    // this deterministic instead of scanning for magic strings.
    out.permissionsSize = u32(raw,0);
    out.buildingsSize = u32(raw,4);
    out.declaredModelSize = u32(raw,8);
    out.declaredCollisionSize = u32(raw,12);
    out.bgsSignature = u16(raw,16);
    out.bgsDataLength = u16(raw,18);
    if(out.bgsSignature!=0x1234){out.error="HGSS map-file BGS signature mismatch";return out;}
    const std::size_t bgsTotal=4u+out.bgsDataLength;
    out.permissionsOffset=16u+bgsTotal;
    out.buildingsOffset=out.permissionsOffset+out.permissionsSize;
    out.modelOffset=out.buildingsOffset+out.buildingsSize;
    out.modelSize=out.declaredModelSize;
    out.collisionOffset=out.modelOffset+out.modelSize;
    out.collisionSize=out.declaredCollisionSize;
    if(out.permissionsOffset>raw.size()||out.buildingsOffset>raw.size()||out.modelOffset>raw.size()||out.collisionOffset>raw.size()||out.collisionOffset+out.collisionSize>raw.size()){
        out.error="HGSS map-file section table exceeds member size";return out;
    }
    if(out.permissionsSize!=2048){out.error="unexpected HGSS permission size";return out;}
    if(out.buildingsSize%48!=0){out.error="HGSS building section is not 48-byte aligned";return out;}
    if(out.modelSize<16||out.modelOffset+4>raw.size()||std::memcmp(raw.data()+out.modelOffset,"BMD0",4)!=0){out.error="declared model section is not BMD0";return out;}
    if(out.collisionSize<16||out.collisionOffset+4>raw.size()||std::memcmp(raw.data()+out.collisionOffset,"BDHC",4)!=0){out.error="declared collision section is not BDHC";return out;}

    out.bgsData.assign(raw.begin()+16,raw.begin()+static_cast<std::ptrdiff_t>(out.permissionsOffset));
    out.permissions.reserve(1024);
    for(std::size_t i=0;i<1024;i++){
        const std::size_t p=out.permissionsOffset+i*2;
        out.permissions.push_back({raw[p],raw[p+1]});
    }
    const std::size_t buildingCount=out.buildingsSize/48;
    out.buildings.reserve(buildingCount);
    for(std::size_t i=0;i<buildingCount;i++){
        const std::size_t p=out.buildingsOffset+i*48;
        HgBuildingPlacement v;
        v.modelId=u32(raw,p+0);
        v.xFraction=u16(raw,p+4);v.x=s16(raw,p+6);
        v.yFraction=u16(raw,p+8);v.y=s16(raw,p+10);
        v.zFraction=u16(raw,p+12);v.z=s16(raw,p+14);
        v.xRotation=u16(raw,p+16);v.yRotation=u16(raw,p+20);v.zRotation=u16(raw,p+24);
        v.width=u16(raw,p+34);v.height=u16(raw,p+40);v.length=u16(raw,p+46);
        out.buildings.push_back(v);
    }

    out.prefix.assign(raw.begin(),raw.begin()+static_cast<std::ptrdiff_t>(out.modelOffset));
    out.collision.assign(raw.begin()+static_cast<std::ptrdiff_t>(out.collisionOffset),raw.begin()+static_cast<std::ptrdiff_t>(out.collisionOffset+out.collisionSize));
    out.bdhc=parse_hgss_bdhc(out.collision);
    std::vector<unsigned char> bmd(raw.begin()+static_cast<std::ptrdiff_t>(out.modelOffset),raw.begin()+static_cast<std::ptrdiff_t>(out.modelOffset+out.modelSize));
    out.model=parse_nsbmd(bmd);
    if(!out.model.valid){out.error="embedded BMD0 parse failed: "+out.model.error;return out;}
    if(!out.bdhc.valid){out.error="BDHC parse failed: "+out.bdhc.error;return out;}
    out.valid=true;
    return out;
}

LandBatchStats validate_land_narc(const std::filesystem::path& landNarc, std::size_t maxMembers) {
    LandBatchStats out;
    auto arc = inspect_narc(landNarc);
    if (!arc.valid) return out;
    out.members = std::min(maxMembers, arc.members.size());
    for (std::size_t i = 0; i < out.members; ++i) {
        auto chunk = load_land_chunk(landNarc, i);
        if (!chunk.valid) { ++out.failures; continue; }
        ++out.parsedMembers;
        if (chunk.modelSize) ++out.membersWithModel;
        if (chunk.collisionSize) {
            ++out.membersWithCollision; out.collisionBytes += chunk.collisionSize;
            if (chunk.bdhc.valid) { ++out.parsedCollisionMembers; out.collisionPlates += chunk.bdhc.plates.size(); }
            if (chunk.modelOffset + chunk.modelSize == chunk.collisionOffset) ++out.exactModelCollisionBoundaries;
        }
        out.models += chunk.model.models.size(); out.textures += chunk.model.textures.size();
        for (auto const& model : chunk.model.models) out.triangles += model.triangles.size();
    }
    return out;
}

bool export_land_model_obj(const LandChunk& chunk, const std::filesystem::path& path) {
    if (!chunk.valid || chunk.model.models.empty()) return false;
    return export_model_obj(chunk.model.models.front(), path);
}

bool hg_permission_allows_land_encounter(const HgPermissionCell& cell){if(!cell.walkable())return false;switch(cell.type){case 0x02: case 0x03: case 0x08:return true;default:return false;}}

bool hg_permission_is_counter(const HgPermissionCell& cell){
    // pokeheartgold's retail MetatileBehavior helper sub_0205B700 is exactly
    // TILE_BEHAVIOR_128 (0x80). The Poké Mart and Pokémon Center service desks
    // use this behavior on the blocking tile between the player and clerk.
    return cell.type==0x80;
}

bool hg_permission_is_ledge_jump(const HgPermissionCell& cell,int dx,int dy){
    // include/constants/metatile_behavior.h in the retail decomp assigns:
    // 56 east, 57 west, 58 north, 59 south. Never infer direction from the
    // collision byte: ledges are one-way field behaviors.
    if(dx==1&&dy==0)return cell.type==56;
    if(dx==-1&&dy==0)return cell.type==57;
    if(dx==0&&dy==-1)return cell.type==58;
    if(dx==0&&dy==1)return cell.type==59;
    return false;
}

int hg_permission_stair_warp_direction(const HgPermissionCell& cell){
    if(cell.type==94)return 1;
    if(cell.type==95)return -1;
    return 0;
}
