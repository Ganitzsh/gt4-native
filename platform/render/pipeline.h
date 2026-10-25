#pragma once
// The display and image-quality pipeline: from a scene rendered at the internal resolution to the
// swapchain image. See docs/RENDERING.md for the stage order and the reasons for it.
//
//   scene (HDR colour, depth, motion vectors, optionally MSAA)  ->  MSAA resolve  ->  exposure
//   temporal path: FSR 2 or TAA(U) to output resolution -> DOF -> motion blur -> bloom -> tonemap -> RCAS
//   spatial path:  DOF -> motion blur -> bloom -> tonemap -> FXAA / SMAA (T2x) -> EASU / resample -> RCAS
//   -> output pass into the content rectangle (aberration, vignette, grain, dither) -> 2D layer
#include "overlay.h"
#include "scene.h"
#include "settings.h"
#include "vk.h"

#include <memory>
#include <string>

namespace gt4 {

class FramePipeline {
public:
    FramePipeline(const vk::Device& device, VkCommandPool pool);
    ~FramePipeline();
    FramePipeline(const FramePipeline&) = delete;
    FramePipeline& operator=(const FramePipeline&) = delete;

    // Applies settings for a swapchain size; rebuilds what changed (waits for the GPU when it must).
    void configure(const Settings& settings, VkExtent2D swapchain, SceneSource& scene);
    // Creates the output pass for the swapchain format (srgbFormat: the format encodes sRGB itself).
    void prepareOutput(VkFormat format, bool srgbFormat, uint32_t outputBits);
    // Per-frame camera inputs for the scene (jitter, sizes, sampler).
    SceneView beginFrame(uint32_t slot, uint32_t frameIndex);
    void record(VkCommandBuffer command, SceneSource& scene, const SceneView& view, vk::Image& swapchainImage,
                Overlay& overlay, float deltaSeconds);
    void resetHistory();

    VkRect2D contentRect() const;
    VkExtent2D renderExtent() const;
    VkExtent2D outputExtent() const;
    // Human-readable summary of the active path (for the on-screen readout and logs).
    std::string describe() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

}  // namespace gt4
