#pragma once
#include "assets/nsbmd.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct BdhcPlate {
    int x=0;
    int y=0;
    int width=0;
    int height=0;
    float z=0.0f;
    float planeD=0.0f;
    int slopeX=0;
    int slopeZ=4096;
    int slopeY=0;
    int type=6; // 0 plane, 1 bridge/plane alias, 2..5 stairs, 6 other
};

struct BdhcData {
    bool valid=false;
    std::string error;
    std::uint16_t numCoords=0;
    std::uint16_t numSlopes=0;
    std::uint16_t numHeights=0;
    std::uint16_t numPlates=0;
    std::vector<BdhcPlate> plates;
};

struct HgPermissionCell {
    std::uint8_t type=0;
    std::uint8_t collision=0;
    bool walkable() const { return collision != 0x80; }
};

struct HgBuildingPlacement {
    std::uint32_t modelId=0;
    std::uint16_t xFraction=0,yFraction=0,zFraction=0;
    std::int16_t x=0,y=0,z=0;
    std::uint16_t xRotation=0,yRotation=0,zRotation=0;
    std::uint16_t width=0,height=0,length=0;
};

struct LandChunk {
    bool valid=false;
    std::string error;
    std::size_t memberIndex=0;
    std::size_t rawSize=0;
    std::uint32_t permissionsSize=0;
    std::uint32_t buildingsSize=0;
    std::uint32_t declaredModelSize=0;
    std::uint32_t declaredCollisionSize=0;
    std::uint16_t bgsSignature=0;
    std::uint16_t bgsDataLength=0;
    std::size_t permissionsOffset=0;
    std::size_t buildingsOffset=0;
    std::size_t modelOffset=0;
    std::size_t modelSize=0;
    std::size_t collisionOffset=0;
    std::size_t collisionSize=0;
    std::vector<unsigned char> prefix;
    std::vector<unsigned char> bgsData;
    std::vector<HgPermissionCell> permissions;
    std::vector<HgBuildingPlacement> buildings;
    std::vector<unsigned char> collision;
    NsbmdMember model;
    BdhcData bdhc;
    const HgPermissionCell* permission_at(int x,int y) const {
        if(x<0||y<0||x>=32||y>=32||permissions.size()!=1024) return nullptr;
        return &permissions[std::size_t(y)*32+std::size_t(x)];
    }
};

struct LandBatchStats {
    std::size_t members=0;
    std::size_t parsedMembers=0;
    std::size_t membersWithModel=0;
    std::size_t membersWithCollision=0;
    std::size_t parsedCollisionMembers=0;
    std::size_t exactModelCollisionBoundaries=0;
    std::size_t models=0;
    std::size_t triangles=0;
    std::size_t textures=0;
    std::size_t collisionBytes=0;
    std::size_t collisionPlates=0;
    std::size_t failures=0;
};

BdhcData parse_hgss_bdhc(const std::vector<unsigned char>& bytes);
bool sample_bdhc_height(const BdhcData& data, float x, float y, float& heightOut);
LandChunk load_land_chunk(const std::filesystem::path& landNarc, std::size_t memberIndex);
LandBatchStats validate_land_narc(const std::filesystem::path& landNarc, std::size_t maxMembers=static_cast<std::size_t>(-1));
bool export_land_model_obj(const LandChunk& chunk, const std::filesystem::path& path);
bool hg_permission_allows_land_encounter(const HgPermissionCell& cell);
// Retail metatile behavior 0x80 is the service-counter interaction tile.
// HG/SS field input may target a map object one tile beyond this blocker.
bool hg_permission_is_counter(const HgPermissionCell& cell);

// Retail HG/SS directional ledges. pokeheartgold names these metatile
// behaviors JUMP_EAST/WEST/NORTH/SOUTH (56..59). A jump is only legal in
// the authored direction; callers still validate the landing tile.
bool hg_permission_is_ledge_jump(const HgPermissionCell& cell,int dx,int dy);

// Retail stair-warp tile behavior. 0x5E walks east across the staircase,
// 0x5F walks west. The field runtime uses this to animate the player across
// authored stair warps instead of teleporting as soon as the warp cell is hit.
int hg_permission_stair_warp_direction(const HgPermissionCell& cell);
