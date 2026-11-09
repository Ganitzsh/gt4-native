#pragma once
// AMD FidelityFX Super Resolution 2.2.1 (temporal upscaling and anti-aliasing) driven through its own
// runtime and Vulkan backend (third_party/fidelityfx-fsr2, MIT), with shaders built by this project.
// The first backend of upscaler.h (B16).
#include "upscaler.h"
#include "vecmath.h"
#include "vk.h"

#include <memory>

namespace gt4 {

using Fsr2Inputs = upscale::Inputs;   // (FSR 2's inputs are the interface's)

class Fsr2 final : public upscale::Upscaler {
public:
    explicit Fsr2(const vk::Device& device);
    ~Fsr2() override;
    Fsr2(const Fsr2&) = delete;
    Fsr2& operator=(const Fsr2&) = delete;
    const char* name() const override { return "FSR 2"; }
    // (Re)creates the context for a display size; returns false (and logs) if FSR 2 cannot run.
    bool create(VkExtent2D displaySize) override;
    void destroy() override;
    bool valid() const override;
    VkExtent2D displaySize() const override;
    void dispatch(VkCommandBuffer command, const Fsr2Inputs& inputs) override;
    int jitterPhaseCount(uint32_t renderWidth, uint32_t displayWidth) const override {
        return phaseCount(renderWidth, displayWidth);
    }
    Vec2 jitterOffset(int index, int phases) const override { return offset(index, phases); }
    // The same, without an instance (pipeline.cpp's jitter until B16 (3) asks the selected upscaler).
    static int phaseCount(uint32_t renderWidth, uint32_t displayWidth);
    static Vec2 offset(int index, int phases);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

}  // namespace gt4
