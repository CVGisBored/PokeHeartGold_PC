#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>

struct NarcMemberInfo {
    std::uint32_t offset=0;
    std::uint32_t size=0;
};
struct NarcArchiveInfo {
    bool valid=false;
    std::uint32_t fileSize=0;
    std::uint16_t blockCount=0;
    std::vector<NarcMemberInfo> members;
};
NarcArchiveInfo inspect_narc(const std::filesystem::path& path);
