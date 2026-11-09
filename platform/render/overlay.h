#pragma once
// The 2D layer: HUD, menus, text and sprites, drawn at output resolution after upscaling and
// post-processing (so it stays sharp and is never blurred, jittered or tonemapped).
//
// Layout follows the game: 2D is authored on a 640 x 480 virtual canvas (GT4's HUD, menu and font
// coordinates, see docs/WIDESCREEN.md). Canvas2D maps it into the content rectangle for any aspect ratio:
// anchored (each element keeps to its edge, as GT4's own 16:9 mode does with its +-96 offsets), centred
// 4:3, or stretched. The R1 renderer will push the game's 2D GS primitives through the same mapping.
#include "sdf_font.h"
#include "settings.h"
#include "ui_renderer.h"
#include "vk.h"

#include <array>
#include <memory>
#include <string>
#include <string_view>

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
    // HDR10 output (PQ, BT.2020): the overlay at `paperWhite` nits; 0: SDR.
    void setHdr(float paperWhite);
    void setFilter(TextureFilter filter);

    // Building a frame: canvas-space (anchored) and pixel-space primitives.
    void begin(uint32_t slot, VkExtent2D target, const Canvas2D& canvas);
    void canvasRect(float x, float y, float w, float h, Color8 color, Anchor anchor);
    // Text in the UI fonts (SDF, E5): `size` is the line's height in canvas units as the 8 x 8 font's cell was, (x, y)
    // its top; the anchor applies to the whole line.
    void canvasText(float x, float y, float size, const std::string& text, Color8 color, Anchor anchor,
                    ui::FontFace face = ui::FontFace::Row);
    void canvasSprite(float x, float y, float size, char glyph, Color8 color, Anchor anchor);  // pixel-art test sprite
    void pixelRect(float x, float y, float w, float h, Color8 color);
    // The 8 x 8 bitmap font, point-sampled: the debug readout's fixed-width text only (E5).
    void pixelText(float x, float y, float scale, const std::string& text, Color8 color);
    // UI-font text in output pixels: `em` pixels, (x, top) the line's top-left; returns its width.
    float uiText(float x, float top, float em, ui::FontFace face, std::string_view text, Color8 color);
    float uiTextWidth(ui::FontFace face, float em, std::string_view text);
    float uiLineHeight(ui::FontFace face, float em) const;
    // The settings pages' layer (MENU_DESIGN M3): a page's draw list through ui::PageRenderer into this sink, drawn
    // after the four batches above, in the order of its calls (a dialog's panel covers the page's text under it).
    // Consecutive shapes, or texts, under one clip share a run (one bind); a clip is rounded inward to whole pixels
    // and clamped to the target, and what it clips to nothing is not drawn. Cleared by begin().
    ui::PageSink& page();
    // Uploads glyphs added while composing (call after composing, before record; waits for the device when the
    // atlas changed, which happens only for a character not drawn before).
    void uploadGlyphs();
    const ui::SdfFontStats& fontStats() const;
    const Canvas2D& canvas() const { return currentCanvas; }
    // Records the draws into the currently bound swapchain rendering (viewport = whole target), the page layer last.
    void record(VkCommandBuffer command);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    Canvas2D currentCanvas;
};

}  // namespace gt4
