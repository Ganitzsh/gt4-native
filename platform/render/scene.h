#pragma once
// The seam between "a frame rendered by the game layer" and the display pipeline.
//
// Today the producer is the test scene (test_scene.cpp). With milestone R1 it becomes the renderer that
// decodes the game's GIF/VIF packets. Whatever produces the frame fills the same targets and camera
// description, and the pipeline (pipeline.cpp) does the rest: anti-aliasing, upscaling, post-processing,
// aspect-ratio fitting, the 2D layer and presentation. See docs/RENDERING.md, "Scene contract".
#include "vecmath.h"
#include "settings.h"
#include "vk.h"

namespace gt4 {

// Formats of the targets a scene renders into (fixed for the lifetime of the pipeline).
struct SceneFormats {
    VkFormat color = VK_FORMAT_R16G16B16A16_SFLOAT;  // linear HDR radiance
    VkFormat velocity = VK_FORMAT_R16G16_SFLOAT;     // previous uv - current uv (unjittered)
    VkFormat depth = VK_FORMAT_D32_SFLOAT;           // reverse Z, infinite far plane
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
};

// Per-frame inputs from the pipeline to the scene.
struct SceneView {
    VkExtent2D renderExtent{};       // internal render resolution
    VkExtent2D outputExtent{};       // content (display) resolution after upscaling
    float aspect = 16.0f / 9.0f;     // content aspect ratio (width / height): the Hor+ projection uses it
    Vec2 jitterPixels;               // sub-pixel offset of this frame, in render pixels (content shift)
    Vec2 jitterClip;                 // the same offset in clip space: add to clip.xy * w
    uint32_t frameIndex = 0;         // monotonically increasing presented-frame counter
    uint32_t slot = 0;               // frame-in-flight index (for per-frame buffers)
    VkSampler materialSampler = VK_NULL_HANDLE;  // 3D texture sampler: filter, anisotropy, LOD bias
    float lodBias = 0.0f;            // the bias baked into materialSampler
    const Settings* settings = nullptr;
};

// What the pipeline needs to know about the camera the frame was rendered with.
struct SceneCamera {
    Mat4 view, projection;           // unjittered
    Mat4 previousViewProjection;     // unjittered
    float near = 0.1f;               // reverse-Z infinite projection
    float verticalFov = 0.87f;       // radians
    bool cut = false;                // camera moved discontinuously: temporal history must reset
};

class SceneSource {
public:
    virtual ~SceneSource() = default;
    // (Re)create format-dependent pipelines.
    virtual void prepare(const SceneFormats& formats) = 0;
    // Work outside the main pass: shadow maps, environment maps, uploads.
    virtual void recordPrepass(VkCommandBuffer command, const SceneView& view) = 0;
    // Draws into the bound main targets (colour 0, velocity 1, depth), viewport = render extent.
    virtual void recordMain(VkCommandBuffer command, const SceneView& view) = 0;
    virtual SceneCamera camera() const = 0;
};

}  // namespace gt4
