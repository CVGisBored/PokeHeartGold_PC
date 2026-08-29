#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct HgTrainerMonRecord {
    std::uint16_t difficulty=0;
    std::uint16_t level=1;
    std::uint16_t species=0;
    std::uint16_t capsule=0;
    std::uint16_t heldItem=0;
    std::array<std::uint16_t,4> moves{};
};

struct HgTrainerRecord {
    bool valid=false;
    std::string error;
    std::uint16_t id=0;
    std::uint8_t partyFlags=0;
    std::uint8_t trainerClass=0;
    std::uint8_t battleType=0;
    std::uint8_t partyCount=0;
    std::array<std::uint16_t,4> items{};
    std::uint32_t aiFlags=0;
    std::uint32_t extra=0;
    std::vector<HgTrainerMonRecord> party;
};

HgTrainerRecord load_hg_trainer(const std::filesystem::path& assetRoot,std::uint16_t trainerId);
