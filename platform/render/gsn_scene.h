#pragma once
// The native renderer's back end (docs/NATIVE_RENDERER.md): executes the front end's frame lists with
// Vulkan at a render scale, into one image per GS frame or Z buffer, and composes the displayed buffer into
// the display pipeline's scene targets. The GS front end (platform/gsnative) runs on the GS worker thread;
// this runs on the render thread.
#include "gs_frontend.h"
#include "scene.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gt4 {

// The scene stage's effects on the game's 3D scenes (docs/NATIVE_RENDERER.md); 0 switches each off.
struct SceneEffects {
    float reflections = 0.0f;        // screen-space reflections: the share of the Fresnel term shown, 0..1
    uint32_t reflectionSteps = 24;
    float fogDensity = 0.0f;         // height fog per metre at the ground, in the course's fog colour
    float shutter = 0.0f;            // motion blur: the exposure, a fraction of the frame
    uint32_t blurSamples = 12;
    float aperture = 0.0f;           // depth of field: the f-number
    float focus = 0.0f;              // ... focused at metres; 0: on the centre of the screen
    uint32_t dofSamples = 32;
};

class GsnScene final : public SceneSource {
public:
    GsnScene(const vk::Device& device, VkCommandPool pool);
    ~GsnScene() override;
    GsnScene(const GsnScene&) = delete;
    GsnScene& operator=(const GsnScene&) = delete;

    // Frame lists in the game's order (moved in): all of them are executed at the next frame, the last one
    // with a VSYNC is shown.
    void addFrames(std::vector<gsn::FrameList>& lists);
    bool hasFrame() const;
    // Running totals for the host's hitch log (main.cpp): what can make one frame slow.
    struct Counters {
        uint64_t framesSkipped = 0, textures = 0, textureBytes = 0, targets = 0, pipelines = 0;
    };
    Counters counters() const;
    std::string textureHeapSummary() const;   // the textures' memory blocks and their free ranges (hitch log)
    // A GS -> host read of drawn pixels: executes the pending lists at once (waiting for the GPU) and packs
    // the target's rectangle as `request.psm`. False when it cannot (nothing prepared yet, no such target).
    bool read(const gsn::ReadRequest& request, std::vector<uint8_t>& out);
    // The game's asynchronous reads (gsn::FrameList::readbacks): copied when their list is drawn; slotDone(slot), once
    // that frame slot's fence is signalled, packs them, and takeReadAnswers hands them over (for GsHost::answerRead).
    struct ReadAnswer {
        uint32_t id = 0, qwc = 0;
        std::vector<uint8_t> pixels;   // packed as the GS keeps them (empty: the target was gone)
    };
    void slotDone(uint32_t slot);
    void takeReadAnswers(std::vector<ReadAnswer>& out);
    // Never left unanswered (the game would wait): lists with such reads not drawn for `after` (a minimised window
    // draws nothing) have them answered with zeros; abandonReadbacks, before the renderer goes, answers all (the GPU's
    // copies, or zeros for lists never drawn).
    void answerUndrawnReadbacks(std::chrono::milliseconds after);
    void abandonReadbacks();
    // Render pixels per GS pixel; 0 = from the render extent (the nearest integer, 1..8).
    void setScale(uint32_t scale);
    // Anisotropic filtering of mipmapped textures (texture.anisotropy; 1 = off). Waits for the GPU on a change.
    void setAnisotropy(int anisotropy);
    // Lighting of the game's 3D scenes (docs/LIGHTING.md): ambient occlusion strength 0..1 (0 = off) and its
    // sample directions, the enhanced grade 0..1 (0 = the game's colours), the display's aspect.
    void setLighting(float occlusion, uint32_t directions, float grade, float aspect);
    // Temporal anti-aliasing of the 3D scenes in the scene stage (render.aa = taa, or FSR 2): jittered draws,
    // motion vectors from the 3D renderers, a resolve against each scene's history (docs/NATIVE_RENDERER.md).
    void setTemporal(bool on);
    // The GPU's time per labelled stage (the GT4_GPU_PROFILE labels), for gt4_host --profile-json: on, the frames
    // are timestamped as with GT4_GPU_PROFILE=1 without its reports; lastFrameStages() gives the last finished frame's
    // stages and milliseconds (empty when off, or before a frame finished). Both on the thread that records this scene
    // (Renderer::draw: gt4_host's main loop), as profileFrame writes the stages while it records.
    void setStageTiming(bool on);
    std::vector<std::pair<std::string, double>> lastFrameStages() const;
    // Presentation decoupled from the game's 60 Hz: at each display refresh between two game frames, the last one
    // drawn again with its moving 3D interpolated (display.frame_interpolation).
    void setInterpolation(bool on);
    void setEffects(const SceneEffects& effects);
    bool antiAliasesItself() const override;

    void prepare(const SceneFormats& formats) override;
    void recordPrepass(VkCommandBuffer command, const SceneView& view) override;
    void recordMain(VkCommandBuffer command, const SceneView& view) override;
    SceneCamera camera() const override;
    bool drawsOriginalEffects() const override { return true; }   // the game draws its own glare and fog
    bool hasMotionVectors() const override { return false; }      // not yet (the GS stream has no history)

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

}  // namespace gt4
