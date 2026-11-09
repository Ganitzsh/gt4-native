#pragma once
// AMD FidelityFX Super Resolution 3.1's upscaler (temporal upscaling and anti-aliasing), from the vendored FidelityFX
// SDK 1.1.4 (third_party/fidelityfx-sdk, MIT; gt4_fsr3) through its own Vulkan backend, with shaders built by this
// project (fsr3_shaders.cpp). The second backend of upscaler.h (PLAN.md B17). No SDK type is in this header: it is
// built into gt4_fsr3_backend, the only library that links gt4_fsr3 and so sees the SDK's forced header
// (ffx3_compat.h); the host sees only upscale::Upscaler.
#include "upscaler.h"
#include "vecmath.h"
#include "vk.h"

#include <memory>

namespace gt4 {

class Fsr3 final : public upscale::Upscaler {
public:
    explicit Fsr3(const vk::Device& device);
    ~Fsr3() override;
    Fsr3(const Fsr3&) = delete;
    Fsr3& operator=(const Fsr3&) = delete;
    const char* name() const override { return "FSR 3.1"; }
    // (Re)creates the context and its three shared images for a display size; false (and logged) if FSR 3.1 cannot
    // run. The render size never exceeds the display size (pipeline.cpp: a temporal upscaler doesn't downsample).
    bool create(VkExtent2D displaySize) override;
    void destroy() override;
    bool valid() const override;
    VkExtent2D displaySize() const override;
    void dispatch(VkCommandBuffer command, const upscale::Inputs& inputs) override;
    int jitterPhaseCount(uint32_t renderWidth, uint32_t displayWidth) const override {
        return phaseCount(renderWidth, displayWidth);
    }
    Vec2 jitterOffset(int index, int phases) const override { return offset(index, phases); }
    // The same, without an instance (pipeline.cpp's jitter for the selected upscaler).
    static int phaseCount(uint32_t renderWidth, uint32_t displayWidth);
    static Vec2 offset(int index, int phases);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

}  // namespace gt4
