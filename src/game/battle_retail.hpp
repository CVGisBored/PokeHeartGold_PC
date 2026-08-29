#pragma once
#include <array>
#include <cstdint>

// Retail HeartGold/SoulSilver encounter selector recovered from the supplied
// ARM9.  The game first classifies a trainer/species into an encounter effect,
// then indexes the adjacent transition/BGM tables.  Keeping this data separate
// from the renderer prevents the native host from inventing music per map.
struct HgRetailBattleCue {
    std::uint8_t effect=42;
    std::uint16_t transition=0xffff;
    std::uint16_t bgm=1116;
};

inline constexpr std::array<std::uint16_t,45> HG_RETAIL_BATTLE_TRANSITION = {
    12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,
    29,30,31,32,33,28,0xffff,0xffff,0xffff,35,36,34,34,
    39,40,41,42,43,44,37,37,38,38,37,38,0xffff,0xffff,45,46
};
inline constexpr std::array<std::uint16_t,45> HG_RETAIL_BATTLE_BGM = {
    1118,1118,1118,1118,1118,1118,1118,1118,
    1127,1127,1127,1127,1127,1127,1127,1127,
    1118,1118,1118,1118,1124,1119,1123,1122,1121,1132,1133,1174,1125,
    1120,1120,1120,1120,1120,1120,1117,1117,1117,1116,1147,1118,1117,1116,1117,1124
};

inline constexpr std::uint8_t hg_retail_trainer_battle_effect(std::uint8_t trainerClass){
    switch(trainerClass){
        case 66:return 0; case 67:return 1; case 70:return 2; case 72:return 3;
        case 74:return 4; case 75:return 5; case 73:return 6; case 76:return 7;
        case 98:return 8; case 103:return 9; case 104:return 10; case 105:return 11;
        case 106:return 12; case 107:return 13; case 108:return 14; case 110:return 15;
        case 87:return 16; case 89:return 17; case 112:return 18; case 88:return 19;
        case 86:return 20; case 23:return 21; case 119:return 21;
        case 55:return 29; case 62:return 29; case 118:return 30; case 117:return 31;
        case 116:return 32; case 114:return 33; case 124:return 34;
        case 47:return 43; case 109:return 44;
        default:return 41; // ordinary trainer encounter
    }
}

inline constexpr std::uint8_t hg_retail_wild_battle_effect(std::uint16_t species){
    switch(species){
        case 243:return 22; // Raikou
        case 244:return 23; // Entei
        case 245:return 24; // Suicune
        case 250:return 25; // Ho-Oh
        case 249:return 26; // Lugia
        case 383:case 382:case 384:return 27; // weather trio
        case 150:case 380:case 381:return 28; // Mewtwo / Lati@s family selector
        default:return 42; // ordinary wild encounter
    }
}

inline constexpr std::uint16_t hg_retail_kanto_battle_bgm(std::uint16_t seq,bool kanto){
    if(!kanto)return seq;
    switch(seq){
        case 1116:return 1125; // wild Kanto
        case 1117:return 1126; // trainer Kanto
        case 1118:return 1127; // gym Kanto
        default:return seq;
    }
}

inline constexpr HgRetailBattleCue hg_retail_battle_cue(bool trainer,std::uint8_t trainerClass,std::uint16_t species,bool kanto){
    HgRetailBattleCue cue{};
    cue.effect=trainer?hg_retail_trainer_battle_effect(trainerClass):hg_retail_wild_battle_effect(species);
    cue.transition=HG_RETAIL_BATTLE_TRANSITION[cue.effect];
    cue.bgm=hg_retail_kanto_battle_bgm(HG_RETAIL_BATTLE_BGM[cue.effect],kanto);
    return cue;
}
