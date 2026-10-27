#pragma once
// The scene source for the PS2's displayed frame: the GS back end scans the frame out (CRTC) and the
// host hands it here each time a new one arrives; it is uploaded and drawn over the whole render area as
// the scene's colour (linear, from an sRGB texture), with zero motion and far depth. The pipeline then
// applies the configured upscaling, anti-aliasing and post-processing; the "original" preset leaves the
// pixels as they are. See docs/GS_BACKEND.md.
#include "scene.h"

#include <cstdint>
#include <memory>

namespace gt4 {

class GsScene final : public SceneSource {
public:
    GsScene(const vk::Device& device, VkCommandPool pool);
    ~GsScene() override;
    GsScene(const GsScene&) = delete;
    GsScene& operator=(const GsScene&) = delete;

    // A new displayed frame (R8G8B8A8, top row first). Copied; uploaded when the next frame is recorded.
    void setFrame(uint32_t width, uint32_t height, const uint32_t* rgba);
    bool hasFrame() const;
    VkExtent2D frameSize() const;

    void prepare(const SceneFormats& formats) override;
    void recordPrepass(VkCommandBuffer command, const SceneView& view) override;
    void recordMain(VkCommandBuffer command, const SceneView& view) override;
    SceneCamera camera() const override;
    bool drawsOriginalEffects() const override { return true; }
    bool hasMotionVectors() const override { return false; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

}  // namespace gt4
