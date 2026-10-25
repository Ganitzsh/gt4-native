#pragma once
// AMD FidelityFX Super Resolution 2.2.1 (temporal upscaling and anti-aliasing) driven through its own
// runtime and Vulkan backend (third_party/fidelityfx-fsr2, MIT), with shaders built by this project.
#include "vecmath.h"
#include "vk.h"

#include <memory>

namespace gt4 {

struct Fsr2Inputs {
    vk::Image* color = nullptr;      // render resolution, HDR linear (not exposed)
    vk::Image* depth = nullptr;      // render resolution, reverse Z (D32 or R32F)
    vk::Image* velocity = nullptr;   // render resolution, previous uv - current uv
    vk::Image* exposure = nullptr;   // 1x1 R32F exposure scale
    vk::Image* output = nullptr;     // display resolution, RGBA16F storage
    VkExtent2D renderSize{};
    Vec2 jitter;                     // pixels, content shift
    float sharpness = 0.0f;          // 0..1, 0 = off
    float deltaMilliseconds = 16.7f;
    bool reset = false;
    float near = 0.1f;
    float verticalFov = 0.87f;
};

class Fsr2 {
public:
    explicit Fsr2(const vk::Device& device);
    ~Fsr2();
    Fsr2(const Fsr2&) = delete;
    Fsr2& operator=(const Fsr2&) = delete;
    // (Re)creates the context for a display size; returns false (and logs) if FSR 2 cannot run.
    bool create(VkExtent2D displaySize);
    void destroy();
    bool valid() const;
    VkExtent2D displaySize() const;
    void dispatch(VkCommandBuffer command, const Fsr2Inputs& inputs);
    static int jitterPhaseCount(uint32_t renderWidth, uint32_t displayWidth);
    static Vec2 jitterOffset(int index, int phaseCount);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

}  // namespace gt4
