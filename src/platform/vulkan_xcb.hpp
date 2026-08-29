#pragma once
#include <cstdint>
#include <memory>
#include <string>
class NativeGame;

class VulkanXcbRenderer {
public:
    VulkanXcbRenderer();
    ~VulkanXcbRenderer();
    VulkanXcbRenderer(const VulkanXcbRenderer&) = delete;
    VulkanXcbRenderer& operator=(const VulkanXcbRenderer&) = delete;
    bool init(uint32_t width, uint32_t height, const std::string& title);
    int run(NativeGame& game);
private:
    struct Impl;
    std::unique_ptr<Impl> p;
};
