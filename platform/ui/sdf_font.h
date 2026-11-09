#pragma once
// The host UI's text (PLAN.md E5, MENU_DESIGN.md 5): the menu fonts, all SIL OFL and unmodified (assets/ui/fonts,
// embedded in gt4_host by cmake/Host.cmake), drawn as single-channel signed distance fields so text stays sharp
// at any size from 720p to 4K. Glyphs are rendered with stb_truetype into one atlas page when first used (ASCII up
// front); a codepoint none of the fonts has is drawn as a box and logged once. No GPU here: the atlas is a byte
// image the caller uploads when dirty (platform/render/overlay.cpp).
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace gt4::ui {

// MENU_DESIGN 5.2's faces: titles and section names, rows, help, the accent serif.
enum class FontFace : uint8_t { Title, Row, Help, Accent };
constexpr int kFontFaceCount = 4;

// One glyph to draw: a quad in the caller's pixels and its rectangle in the atlas (0..1).
struct GlyphQuad {
    float x0, y0, x1, y1;
    float u0, v0, u1, v1;
};

struct SdfFontStats {
    double buildMs = 0.0;          // the constructor's time (fonts parsed, ASCII rendered)
    size_t atlasBytes = 0;         // the atlas page's memory
    int glyphs = 0;                // glyphs in the atlas
    int missing = 0;               // codepoints none of the fonts has (each counted once)
    bool full = false;             // the page ran out of room (later glyphs drawn as boxes)
};

class SdfFont {
public:
    static constexpr uint32_t kAtlasSize = 2048;   // one R8 page
    static constexpr float kBasePixels = 48.0f;    // the em the distance fields are rendered at
    static constexpr int kSpread = 6;              // distance field reach around each glyph, in base pixels

    SdfFont();
    ~SdfFont();
    SdfFont(const SdfFont &) = delete;
    SdfFont &operator=(const SdfFont &) = delete;

    // Lays out UTF-8 `text` in `face` at an em of `em` pixels, the baseline at (x, baseline): appends one quad per
    // visible glyph and returns the advance (the text's width). Glyphs not yet in the atlas are rendered into it.
    float layout(FontFace face, float em, std::string_view text, float x, float baseline, std::vector<GlyphQuad> &out);
    float measure(FontFace face, float em, std::string_view text);   // layout's width, nothing added
    float ascent(FontFace face, float em) const;                      // baseline to the top of the line box
    float lineHeight(FontFace face, float em) const;                  // ascent + descent + line gap

    // The atlas: kAtlasSize x kAtlasSize bytes, 128 at a glyph's edge, more inside. takeDirty() says whether it
    // changed since the last call (the caller uploads it then).
    const std::vector<uint8_t> &atlas() const;
    bool takeDirty();
    const SdfFontStats &stats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// Decodes the next codepoint of UTF-8 `text` at `pos` and advances it; malformed bytes give U+FFFD.
uint32_t nextCodepoint(std::string_view text, size_t &pos);

}  // namespace gt4::ui
