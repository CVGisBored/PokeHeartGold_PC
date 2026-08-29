#pragma once
#include <cstddef>

// Member layout for HeartGold/SoulSilver's retail New Game intro archive
// (demo/intro/intro.narc, extracted in NitroFS as a/1/2/0).
// Keeping these pairings in one place prevents cross-wiring NCGR graphics with
// another actor's NCLR palette, which was the source of the v0.23 Ethan/Lyra
// color corruption.
namespace hg_new_game_asset {
constexpr std::size_t OakGfx = 10;
constexpr std::size_t OakPalette = 11;
constexpr std::size_t OakCell = 53;

constexpr std::size_t BoyCellGfx = 12;
constexpr std::size_t BoyPoseGfx = 13;
constexpr std::size_t BoyPalette = 16;
constexpr std::size_t BoyCell = 55;

constexpr std::size_t GirlCellGfx = 17;
constexpr std::size_t GirlPoseGfx = 18;
constexpr std::size_t GirlPalette = 21;
constexpr std::size_t GirlCell = 57;

constexpr std::size_t MarillPalette = 63;
constexpr std::size_t MarillGfx = 64;
constexpr std::size_t MarillCell = 65;
} // namespace hg_new_game_asset
