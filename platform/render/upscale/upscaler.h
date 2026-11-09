#pragma once
// A temporal upscaler: the display side's reconstruction from the scene's frame at a render extent R to the output
// extent (PLAN.md B16; docs/modern/CONTRACTS.md b.11, DisplayInputs). Backends: FSR 2 (fsr2.h); FSR 3.1 is B17's.
// The display stages (display_feature.cpp) hold one and call it in their temporal reconstruction pass.
#include "vecmath.h"
#include "vk.h"

namespace gt4::upscale {

// One frame's inputs, in CONTRACTS b.11's conventions.
struct Inputs {
    vk::Image* color = nullptr;      // render extent, HDR linear (not exposed)
    vk::Image* depth = nullptr;      // render extent, in the source's stated convention (b.11)
    vk::Image* velocity = nullptr;   // render extent, previous less current as a fraction of R, the jitter removed
    vk::Image* exposure = nullptr;   // 1x1 R32F exposure scale
    vk::Image* output = nullptr;     // output extent, RGBA16F storage
    // Optional R8 masks at the render extent (null: none). Reactive: how much the current frame should replace the
    // history (particles, alpha-blended effects); transparency and composition: pixels whose shading the velocity
    // does not describe (reflections, UV-animated textures).
    vk::Image* reactive = nullptr;
    vk::Image* transparencyAndComposition = nullptr;
    VkExtent2D renderSize{};
    Vec2 jitter;                     // render pixels, the content's shift this frame
    float sharpness = 0.0f;          // 0..1, 0 = off
    float deltaMilliseconds = 16.7f;
    bool reset = false;
    float nearPlane = 0.1f;          // (not `near`: windows.h defines near and far as empty macros)
    float verticalFov = 0.87f;
};

class Upscaler {
public:
    virtual ~Upscaler() = default;
    virtual const char* name() const = 0;
    // (Re)creates the backend for an output extent; false (and logged) when it cannot run.
    virtual bool create(VkExtent2D displaySize) = 0;
    virtual void destroy() = 0;
    virtual bool valid() const = 0;
    virtual VkExtent2D displaySize() const = 0;
    virtual void dispatch(VkCommandBuffer command, const Inputs& inputs) = 0;
    // The jitter sequence the backend wants: its length for this render and output width, and the offset (render
    // pixels) of phase `index`.
    virtual int jitterPhaseCount(uint32_t renderWidth, uint32_t displayWidth) const = 0;
    virtual Vec2 jitterOffset(int index, int phaseCount) const = 0;
};

}  // namespace gt4::upscale
