#pragma once
#include "game/render.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <array>

struct Vec2f { float x=0, y=0; };
struct Vec3f { float x=0, y=0, z=0; };
struct NsbmdVertex {
    Vec3f position{};
    Vec3f normal{0,1,0};
    Vec2f uv{};
    Color color{1,1,1,1};
};
using NsbmdMatrix = std::array<float,16>;

struct NsbmdTriangle {
    NsbmdVertex a{}, b{}, c{};
    Color materialColor{1,1,1,1};
    int materialIndex=-1;
    int textureIndex=-1;
    int paletteIndex=-1;
    Color ambientColor{1,1,1,1};
    Color emissionColor{0,0,0,1};
    std::uint32_t polygonAttr=0;
    std::uint32_t texImageParams=0;
    bool lightingEnabled=false;
    // Build-model joint that owned this draw call. -1 for unjointed geometry.
    int jointIndex=-1;
    // Renderer-invariant material/vertex/face-light result. Field time-of-day
    // brightness is applied later; baking this once avoids recalculating the
    // same face normal and material math every frame.
    Color rasterBaseColor{1,1,1,1};
};
struct NsbmdModel {
    std::string name;
    std::vector<NsbmdTriangle> triangles;
    Vec3f min{0,0,0}, max{0,0,0};
    std::size_t sourcePieces=0;
    std::size_t sourceVertices=0;
    std::vector<std::string> materialNames;
    std::vector<std::string> materialTextureNames;
    std::vector<std::string> materialPaletteNames;
    // Nitro G3D models store an up/down position scale used by their render
    // command stream. In HG/SS, terrain uses upScale=64 while smaller placed
    // props commonly use 4 or 16. normalizedScale converts that DS position
    // scale into the native runtime's map units (terrain raw vertices are
    // already normalized to upScale 64).
    float upScale=1.0f;
    float downScale=1.0f;
    float normalizedScale=1.0f;
    // Bind-pose joint metadata recovered from the NSBMD SBC stream. These are
    // used by BCA0 animations without re-decoding geometry every frame.
    std::vector<NsbmdMatrix> jointLocalMatrices;
    std::vector<NsbmdMatrix> jointBindWorldMatrices;
    std::vector<int> jointParents;
};
struct NsbmdTexture {
    std::string name;
    std::uint32_t width=0, height=0;
    std::uint8_t format=0;
    std::vector<unsigned char> rgba;
    // Palette-dependent decoded variants for indexed DS texture formats.
    std::vector<std::vector<unsigned char>> paletteVariants;
};
struct NsbmdMember {
    bool valid=false;
    std::string error;
    std::vector<NsbmdModel> models;
    std::size_t textureCount=0;
    std::size_t paletteCount=0;
    std::vector<std::string> paletteNames;
    std::vector<NsbmdTexture> textures;
};
struct NsbtaMaterialTrack {
    std::string materialName;
    std::vector<float> uTranslation;
    std::vector<float> vTranslation;
};
struct NsbtaAnimation {
    bool valid=false;
    std::string error;
    std::uint16_t frameCount=0;
    std::vector<NsbtaMaterialTrack> tracks;
};

struct NsbcaJointTrack {
    int jointIndex=-1;
    std::vector<NsbmdMatrix> localFrames;
};
struct NsbcaAnimation {
    bool valid=false;
    std::string error;
    std::uint16_t frameCount=0;
    std::vector<NsbcaJointTrack> tracks;
};

struct NsbmdBatchStats {
    std::size_t members=0;
    std::size_t parsedMembers=0;
    std::size_t models=0;
    std::size_t triangles=0;
    std::size_t vertices=0;
    std::size_t textures=0;
    std::size_t failures=0;
};

std::vector<unsigned char> read_narc_member(const std::filesystem::path& path, std::size_t index);
NsbmdMember parse_nsbmd(const std::vector<unsigned char>& bytes);
NsbmdMember parse_nitro_texture_container(const std::vector<unsigned char>& bytes);
NsbmdMember load_nsbmd_from_narc(const std::filesystem::path& path, std::size_t index);
NsbmdMember load_nitro_texture_from_narc(const std::filesystem::path& path, std::size_t index);
NsbtaAnimation parse_nsbta(const std::vector<unsigned char>& bytes);
NsbtaAnimation load_nsbta_from_narc(const std::filesystem::path& path, std::size_t index);
NsbtaAnimation load_build_model_nsbta(const std::filesystem::path& animationListNarc, const std::filesystem::path& animationNarc, std::size_t modelIndex);
NsbcaAnimation parse_nsbca(const std::vector<unsigned char>& bytes);
NsbcaAnimation load_nsbca_from_narc(const std::filesystem::path& path, std::size_t index);
NsbcaAnimation load_build_model_nsbca(const std::filesystem::path& animationListNarc, const std::filesystem::path& animationNarc, std::size_t modelIndex);
NsbmdMatrix sample_nsbca_joint_delta(const NsbmdModel& model, const NsbcaAnimation& animation, int jointIndex, double seconds, double framesPerSecond=60.0);
Vec3f transform_nsbmd_point(const NsbmdMatrix& matrix, Vec3f point);
Vec3f transform_nsbmd_normal(const NsbmdMatrix& matrix, Vec3f normal);
Vec2f sample_nsbta_uv(const NsbtaAnimation& animation, const std::string& materialName, double seconds, double framesPerSecond=60.0);
void bind_nsbmd_external_textures(NsbmdMember& modelMember, const NsbmdMember& textureMember);
NsbmdBatchStats validate_nsbmd_narc(const std::filesystem::path& path, std::size_t maxMembers=static_cast<std::size_t>(-1));
bool export_model_obj(const NsbmdModel& model, const std::filesystem::path& path);
bool export_texture_ppm(const NsbmdTexture& texture, const std::filesystem::path& path);
