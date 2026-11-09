#pragma once
// The display pipeline's stages after the scene, as a render graph feature (ticket B3, docs/modern/CONTRACTS.md
// b.9): exposure, FSR 2 / TAA, depth of field, motion blur, bloom, tone mapping, FXAA / SMAA (T2x), EASU / resample,
// RCAS, the output pass and the 2D layer. FramePipeline (pipeline.cpp) keeps the settings, the configuration, the
// scene targets, the scene pass and its MSAA resolve; it hands this feature its inputs on the Blackboard.
#include "overlay.h"
#include "render_graph.h"
#include "scene.h"
#include "settings.h"
#include "vk.h"

#include <memory>

namespace gt4 {

// The path the settings select, as far as the display stages need it (FramePipeline's configuration).
struct DisplayConfig {
    VkRect2D content{};
    VkExtent2D render{}, output{}, post{};
    uint32_t msaa = 1;
    bool fsr = false, taa = false;   // fsr: a temporal upscaler runs, `upscaler` (FSR 2 or FSR 3.1)
    AntiAliasing spatialAa = AntiAliasing::Off;
    Upscaler upscaler = Upscaler::Bilinear;
};

// Written by the scene pass when it runs (the camera the frame was rendered with, whether temporal history resets);
// read by the display passes when they run, never while they are built.
struct DisplaySceneState {
    SceneCamera camera{};
    bool reset = false;
};

// The display stages' inputs for one frame, on the Blackboard before DisplayFeature::build.
struct DisplayInputs {
    // The scene's images (render resolution; depth: the resolved one under MSAA) and their handles after the scene pass.
    vk::Image* color = nullptr;
    vk::Image* velocity = nullptr;
    vk::Image* depth = nullptr;
    rg::Texture colorHandle, velocityHandle, depthHandle;
    SceneSource* scene = nullptr;                      // FSR 2 masks, drawsOriginalEffects
    const DisplaySceneState* sceneState = nullptr;     // filled when the scene pass runs
    vk::Image* target = nullptr;                       // the swapchain image
    Overlay* overlay = nullptr;                        // the 2D layer, drawn in the output pass
    float dt = 0.0f;                                   // seconds
    Vec2 jitter;                                       // this frame's sub-pixel offset, render pixels
    DisplayConfig config;
    uint32_t frameIndex = 0;
};

class DisplayFeature : public rg::Feature {
public:
    DisplayFeature(const vk::Device& device, VkCommandPool pool);
    ~DisplayFeature() override;
    DisplayFeature(const DisplayFeature&) = delete;
    DisplayFeature& operator=(const DisplayFeature&) = delete;

    const char* name() const override { return "display"; }
    // The passes from "exposure: clear" through "output", from the DisplayInputs on the board (none: no passes).
    void build(rg::Graph& graph, rg::Blackboard& board, const rg::FrameInfo& frame) override;

    // Targets for a configuration (after FramePipeline's own): FSR 2 and its output, the work images' descriptions.
    // False when FSR 2 cannot run on this device (logged): the caller resolves the configuration without FSR 2 and
    // makes its targets again.
    bool configure(const DisplayConfig& config);
    void releaseTargets();
    void reportMemory() const;                     // the work images and histories (after configure)
    void reportTransients(const rg::Plan& plan);   // the graph transients' memory, when it changed (after compile)
    void prepareOutput(VkFormat format, bool srgbFormat, uint32_t outputBits);
    void setHdr(float paperWhite, float peak);
    void resetExposure();
    void endFrame();   // after the graph executed (or failed to compile): drops this frame's inputs (its pointers)
    VkSampler pointSampler() const;                // nearest, clamped (also FramePipeline's MSAA resolve's)

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

}  // namespace gt4
