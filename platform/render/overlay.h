#pragma once
// The 2D layer: HUD, menus, text and sprites, drawn at output resolution after upscaling and
// post-processing (so it stays sharp and is never blurred, jittered or tonemapped).
//
// Layout follows the game: 2D is authored on a 640 x 480 virtual canvas (GT4's HUD, menu and font
// coordinates, see docs/WIDESCREEN.md). Canvas2D maps it into the content rectangle for any aspect ratio:
// anchored (each element keeps to its edge, as GT4's own 16:9 mode does with its +-96 offsets), centred
// 4:3, or stretched. The R1 renderer will push the game's 2D GS primitives through the same mapping.
#include "settings.h"
#include "vk.h"

#include <array>
#include <memory>
#include <string>

namespace gt4 {

enum class Anchor : uint8_t { Left, Center, Right };

struct Canvas2D {
    static constexpr float kWidth = 640.0f, kHeight = 480.0f;
    VkRect2D content{};          // content rectangle in swapchain pixels
    HudLayout layout = HudLayout::Anchored;
    float safeArea = 0.0f;       // fraction of the content height

    float scaleX() const;        // swapchain pixels per canvas unit
    float scaleY() const;
    float x(float canvasX, Anchor anchor) const;
    float y(float canvasY) const;
};

struct Color8 { uint8_t r, g, b, a; };

class Overlay {
public:
    Overlay(const vk::Device& device, VkCommandPool pool);
    ~Overlay();
    Overlay(const Overlay&) = delete;
    Overlay& operator=(const Overlay&) = delete;

    void prepare(VkFormat swapchainFormat, bool srgbTarget);
    void setFilter(TextureFilter filter);

    // Building a frame: canvas-space (anchored) and pixel-space primitives.
    void begin(uint32_t slot, VkExtent2D target, const Canvas2D& canvas);
    void canvasRect(float x, float y, float w, float h, Color8 color, Anchor anchor);
    void canvasText(float x, float y, float size, const std::string& text, Color8 color, Anchor anchor);
    void canvasSprite(float x, float y, float size, char glyph, Color8 color, Anchor anchor);  // pixel-art test sprite
    void pixelRect(float x, float y, float w, float h, Color8 color);
    void pixelText(float x, float y, float scale, const std::string& text, Color8 color);  // always point-sampled
    const Canvas2D& canvas() const { return currentCanvas; }
    // Records the draws into the currently bound swapchain rendering (viewport = whole target).
    void record(VkCommandBuffer command);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    Canvas2D currentCanvas;
};

}  // namespace gt4
