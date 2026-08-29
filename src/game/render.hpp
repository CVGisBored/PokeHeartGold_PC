#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct Color {
    float r=0, g=0, b=0, a=1;
};

struct DrawRect {
    float x=0, y=0, w=0, h=0;
    Color color{};
};

struct RenderText {
    float x=0, y=0;
    int scale=2;
    std::string text;
    Color color{1,1,1,1};
    bool shadow=true;
    // Optional explicit glyph/line advance. Zero preserves the legacy UI
    // metrics; dialogue uses wider retail-like tracking without stretching
    // every menu/debug label in the application.
    float advance=0.0f;
    float lineAdvance=0.0f;
};

struct RenderFrame {
    static constexpr float LogicalWidth = 1280.0f;
    static constexpr float LogicalHeight = 720.0f;
    static constexpr int PixelWidth = 1280;
    static constexpr int PixelHeight = 720;

    Color clear{0.02f,0.025f,0.035f,1.0f};

    // Optional full-resolution RGBA8 scene raster.  v0.6.2 uses this for the
    // ROM world so authentic DS textures can be sampled one screen pixel at a
    // time with a real Z buffer instead of being approximated by large Vulkan
    // clear rectangles. UI rectangles/text are composited on top by the
    // Vulkan presenter.
    std::vector<std::uint8_t> rgba;

    std::vector<DrawRect> rects;
    std::vector<RenderText> texts;

    bool hasPixels() const {
        return rgba.size()==std::size_t(PixelWidth)*PixelHeight*4;
    }
};
