#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
struct HgWildSlot{std::uint16_t species=0;std::uint8_t minLevel=1,maxLevel=1;};
struct HgWildTable{bool valid=false;std::string error;std::uint8_t walkingRate=0;std::vector<HgWildSlot> morning,day,night;};
HgWildTable load_hg_wild_table(const std::filesystem::path& assets,std::size_t bank);
HgWildSlot choose_hg_land_encounter(const HgWildTable& t,std::uint32_t roll,int hour=12);
