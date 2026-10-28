#pragma once
// The native renderer's back end (docs/NATIVE_RENDERER.md): executes the front end's frame lists with
// Vulkan at a render scale, into one image per GS frame or Z buffer, and composes the displayed buffer into
// the display pipeline's scene targets. The GS front end (platform/gsnative) runs on the GS worker thread;
// this runs on the render thread.
#include "gs_frontend.h"
#include "scene.h"

#include <cstdint>
#include <memory>

namespace gt4 {

class GsnScene final : public SceneSource {
public:
    GsnScene(const vk::Device& device, VkCommandPool pool);
    ~GsnScene() override;
    GsnScene(const GsnScene&) = delete;
    GsnScene& operator=(const GsnScene&) = delete;

    // The newest frame list (swapped: the caller gets the previous one's buffers back).
    void setFrame(gsn::FrameList& list);
    bool hasFrame() const;
    // Render pixels per GS pixel; 0 = from the render extent (the nearest integer, 1..8).
    void setScale(uint32_t scale);
    // Anisotropic filtering of mipmapped textures (texture.anisotropy; 1 = off). Waits for the GPU on a change.
    void setAnisotropy(int anisotropy);

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
