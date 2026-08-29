#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct HgPersonalData {
    bool valid=false;
    std::uint8_t hp=1,attack=1,defense=1,speed=1,spAttack=1,spDefense=1;
    std::uint8_t type1=0,type2=0;
    std::uint8_t catchRate=45,baseExp=0;
    std::uint16_t item1=0,item2=0;
    std::uint8_t genderRatio=127,eggCycles=20,baseFriendship=70,growthRate=0;
    std::uint8_t ability1=0,ability2=0;
};

struct HgMoveData {
    bool valid=false;
    std::uint16_t effect=0;
    std::uint8_t category=2; // 0 physical, 1 special, 2 status
    std::uint8_t power=0,type=0,accuracy=100,pp=0,effectChance=0;
    std::int8_t priority=0;
};

struct HgItemData {
    bool valid=false;
    std::uint16_t price=0;
    std::uint8_t fieldPocket=0,battlePocket=0;
    std::uint8_t fieldUseFunc=0,battleUseFunc=0,partyUse=0;
};

bool hg_initialize_pokemon_database(const std::filesystem::path& assets);
const HgPersonalData* hg_personal_data(std::uint16_t species);
const HgMoveData* hg_move_data(std::uint16_t move);
const HgItemData* hg_item_data(std::uint16_t item);
std::vector<std::uint16_t> hg_levelup_moves(std::uint16_t species,std::uint8_t level);
std::string hg_rom_species_name(std::uint16_t species);
std::string hg_rom_move_name(std::uint16_t move);
std::string hg_rom_item_name(std::uint16_t item);
std::size_t hg_species_count();
std::size_t hg_move_count();
std::size_t hg_item_count();
std::uint32_t hg_exp_for_level(std::uint16_t species,std::uint8_t level);
std::uint8_t hg_level_for_exp(std::uint16_t species,std::uint32_t exp);
const char* hg_type_name(std::uint8_t type);
float hg_type_effectiveness(std::uint8_t attackType,std::uint8_t defendType);
