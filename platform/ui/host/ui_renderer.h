#pragma once
// The settings pages' renderer (docs/modern/MENU_DESIGN.md M3): a page's draw list (draw_list.h, as GT4_REC_UI
// carries it) turned into output-pixel shapes and UI-font text for a PageSink. The overlay is the real sink
// (Overlay's page layer, drawn last, in the list's order); tests/ui_renderer.cpp records the calls instead. Host only:
// no Vulkan here, but the overlay's sink is (platform/ui/host, game_link's shared-source check).
//
// The canvas (draw_list.h: 1080 units high, canvasWidth wide) is scaled by content.height / 1080 and centred in the
// content rectangle, so at an aspect ratio narrower than the window the page lives inside the pillarbox, over the
// game's picture, never over the black bars (MENU_DESIGN 3.6).
//
// Until M10's assets (MENU_DESIGN M10) nothing is packed: Quad and NineSlice draw flat in the item's colour, and an
// asset ID other than 0 is logged once. Text wraps at spaces when its box holds more than one line (h at least two
// line heights), and what still does not fit is cut at a code point with an ellipsis: right for the pages' Latin
// text, not grapheme-aware (a page with combining marks would need a grapheme cut).
#include "draw_list.h"
#include "sdf_font.h"

#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gt4::ui {

struct PageColor {
    uint8_t r = 255, g = 255, b = 255, a = 255;
    bool operator==(const PageColor &o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
};

// A rectangle in output pixels (floats: the canvas maps onto fractional pixels).
struct PageRect {
    float x = 0, y = 0, w = 0, h = 0;
};

class PageSink {
public:
    virtual ~PageSink() = default;
    // A rectangle in output pixels, `top` at its top edge and `bottom` at its bottom: equal for a flat quad, two for a
    // gradient. The two colours interpolate per vertex in the sink's colour space as given (sRGB-encoded 8-bit values,
    // as every overlay colour is; the overlay blends in that space, like the PS2's 2D layer).
    virtual void rect(const PageRect &rect, PageColor top, PageColor bottom) = 0;
    virtual float textWidth(FontFace face, float em, std::string_view text) = 0;
    virtual float lineHeight(FontFace face, float em) = 0;   // one line's advance, for wrapped text
    // UI-font text, `em` pixels, (x, top) the line's top-left in output pixels.
    virtual void text(float x, float top, float em, FontFace face, std::string_view text, PageColor color) = 0;
    // Later shapes and text are clipped to `clip` (output pixels), or not clipped when it is null.
    virtual void clip(const PageRect *clip) = 0;
};

// The draw list's font families on the UI faces (MENU_DESIGN 5.2; draw_list.h: the wide grotesque, its text cut, the
// serif): Title italic is the titles' Saira Semi Expanded bold italic, Title upright the rows' Saira Semi Expanded,
// Body the help's Saira, Accent Libre Caslon Text. The faces are static fonts, so an item's weight is drawn in its
// face's own (the layout's 600 and 700 on rows are the design's emphasis, not a missing file), and italic Body or
// Accent text, which has no italic face, is drawn upright (noted once by PageRenderer).
FontFace pageFace(Font font, bool italic);
// 0xRRGGBBAA times opacity.
PageColor pageColor(uint32_t rgba, float opacity);
// The longest code-point prefix of `text` that fits `width` pixels with an ellipsis after it (U+2026), or `text`
// whole when it fits.
std::string fitText(PageSink &sink, FontFace face, float em, std::string_view text, float width);
// `text` in lines of at most `width` pixels, at most `maxLines` of them: broken at spaces (and at '\n'), a word wider
// than the width broken at a code point; what does not fit in the last line is cut there with an ellipsis.
std::vector<std::string> wrapText(PageSink &sink, FontFace face, float em, std::string_view text, float width,
                                  int maxLines);

class PageRenderer {
public:
    // Draws `list` into `content` (output pixels: the content rectangle the game's picture fills).
    void draw(const DrawList &list, const PageRect &content, PageSink &sink);
    // The notes logged so far (each once: an unknown asset, italic text with no italic face, an unbalanced ClipPop,
    // an item kind this renderer does not know); printed as they come, kept for the tests.
    const std::vector<std::string> &notes() const { return notes_; }

    // Wrapped texts kept (M9a): a page drawn every frame is wrapped and measured once per text, face, em, width and
    // line count. Entries unused for kKeepDraws draws go once there are more than kCacheSize. The measurements come
    // from the sink, so a renderer draws for one sink (the overlay's) only.
    static constexpr size_t kCacheSize = 512;
    static constexpr uint32_t kKeepDraws = 120;
    size_t cached() const { return wrapped_.size(); }

private:
    struct Wrapped {
        std::vector<std::string> lines;
        std::vector<float> widths;
        uint32_t used = 0;
    };
    const Wrapped &wrap(PageSink &sink, FontFace face, float em, const std::string &text, float width, int lines);
    void note(const std::string &text);
    std::set<std::string> noted_;
    std::vector<std::string> notes_;
    std::unordered_map<std::string, Wrapped> wrapped_;
    uint32_t draws_ = 0;
};

}  // namespace gt4::ui
