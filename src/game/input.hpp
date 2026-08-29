#pragma once
#include <array>
#include <cstddef>

enum class GameButton : std::size_t {
    Up, Down, Left, Right,
    Interact, Menu, Run,
    Save, Load, Debug, Reset, Assets, Terrain,
    Quit,
    Count
};

struct InputState {
    std::array<bool, static_cast<std::size_t>(GameButton::Count)> down{};
    std::array<bool, static_cast<std::size_t>(GameButton::Count)> pressed{};
    std::array<bool, static_cast<std::size_t>(GameButton::Count)> released{};
    float mouseX=0.0f,mouseY=0.0f;
    bool mouseDown=false,mousePressed=false,mouseReleased=false,mouseInside=false;

    bool isDown(GameButton b) const { return down[static_cast<std::size_t>(b)]; }
    bool wasPressed(GameButton b) const { return pressed[static_cast<std::size_t>(b)]; }
    bool wasReleased(GameButton b) const { return released[static_cast<std::size_t>(b)]; }
    void clearEdges() { pressed.fill(false); released.fill(false); mousePressed=false; mouseReleased=false; }
};
