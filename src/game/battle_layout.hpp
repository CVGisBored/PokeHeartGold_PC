#pragma once

#include <array>

struct HgBattleUiRect {
    float x=0, y=0, w=0, h=0;
    constexpr bool contains(float px,float py) const {
        return px>=x && py>=y && px<x+w && py<y+h;
    }
};

// PC-native single-screen battle layout.  The retail battle artwork remains in
// the upper field; the DS touch commands are promoted to ordinary clickable
// controls in the lower command panel instead of rendering a second screen.
inline constexpr std::array<HgBattleUiRect,4> HG_BATTLE_MAIN_RECTS={{
    {690,552,235,62}, // FIGHT
    {945,552,235,62}, // BAG
    {690,628,235,62}, // POKEMON
    {945,628,235,62}, // RUN
}};

inline constexpr std::array<HgBattleUiRect,4> HG_BATTLE_MOVE_RECTS={{
    {475,548,340,66},
    {830,548,340,66},
    {475,626,340,66},
    {830,626,340,66},
}};

inline constexpr HgBattleUiRect HG_BATTLE_CANCEL_RECT={1175,626,85,66};
