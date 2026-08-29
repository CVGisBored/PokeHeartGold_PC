#pragma once
#include "game/input.hpp"
#include "game/render.hpp"
#include <filesystem>
#include <memory>
#include <string>

class NativeGame {
public:
    NativeGame(std::filesystem::path assets, std::filesystem::path savePath);
    ~NativeGame();
    NativeGame(const NativeGame&) = delete;
    NativeGame& operator=(const NativeGame&) = delete;

    bool validate();
    void update(const InputState& input, double dtSeconds);
    RenderFrame render() const;
    bool wantsQuit() const;

    bool save();
    bool load();
    void resetGame();
    bool battleTurnSequenceTest();
    bool battleRenderVisibilityTest();
    bool fieldCounterInteractionTest();
    RenderFrame battleRenderRegressionFrame();

private:
    struct Impl;
    std::unique_ptr<Impl> p;
};
