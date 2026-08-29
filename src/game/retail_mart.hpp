#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct HgRetailMartItem {
    std::uint16_t id=0;
    std::uint32_t price=0; // 0 means use the retail item database price.
    std::uint16_t slot=0;  // retail list slot, used by Pokéathlon sold flags.
};

std::vector<HgRetailMartItem> hg_standard_mart_inventory(unsigned badgeCount);
std::vector<HgRetailMartItem> hg_special_mart_inventory(std::uint16_t which);
std::vector<HgRetailMartItem> hg_decoration_mart_inventory(std::uint16_t which);
std::vector<HgRetailMartItem> hg_seal_mart_inventory(std::uint16_t which);
std::vector<HgRetailMartItem> hg_pokeathlon_daily_inventory(unsigned weekday,bool nationalDex);
std::vector<HgRetailMartItem> hg_pokeathlon_data_card_inventory(unsigned purchasedCount);
std::string hg_decoration_name(std::uint16_t id);
std::string hg_seal_name(std::uint16_t id);
