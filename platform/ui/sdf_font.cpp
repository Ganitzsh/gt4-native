#include "sdf_font.h"

#include "stb_truetype.h"
#include "ui_fonts.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace gt4::ui {

namespace {

// MENU_DESIGN 5.2: titles and section names in Saira Semi Expanded bold italic, rows in Saira Semi Expanded, help in
// Saira, the accent in Libre Caslon Text (static upstream TTFs, assets/ui/fonts).
const char *const kFaceFiles[kFontFaceCount] = {"SairaSemiExpanded-BoldItalic", "SairaSemiExpanded-Regular",
                                                 "Saira-Regular", "LibreCaslonText-Regular"};
// Where a codepoint missing from a face is looked for next.
const FontFace kFallback[] = {FontFace::Row, FontFace::Help, FontFace::Title, FontFace::Accent};
constexpr uint32_t kBoxCodepoint = 0xFFFFFFFFu;   // the drawn box (a codepoint no font has)
constexpr uint8_t kOnEdge = 128;
constexpr float kDistanceScale = 128.0f / static_cast<float>(SdfFont::kSpread);

struct Glyph {
    float advance = 0.0f;                         // base pixels
    float xoff = 0.0f, yoff = 0.0f, w = 0.0f, h = 0.0f;   // the bitmap's place from the pen, base pixels
    float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;
    int index = 0;                                // the glyph in its font, for kerning (0: none)
    int font = -1;                                // the face the glyph came from
    bool visible = false;
};

uint64_t key(FontFace face, uint32_t codepoint) { return (static_cast<uint64_t>(face) << 32) | codepoint; }

}  // namespace

uint32_t nextCodepoint(std::string_view text, size_t &pos) {
    const auto byte = [&](size_t i) { return static_cast<uint8_t>(text[i]); };
    const uint8_t first = byte(pos++);
    if (first < 0x80) return first;
    // Lead bytes: C2-DF one continuation byte, E0-EF two, F0-F7 three (F5-F7 and F4's tail exceed U+10FFFF and are
    // refused below); 80-C1 and F8-FF lead nothing.
    const int extra = first >= 0xC2 && first < 0xE0   ? 1
                      : first >= 0xE0 && first < 0xF0 ? 2
                      : first >= 0xF0 && first < 0xF8 ? 3
                                                      : -1;
    if (extra < 0) return 0xFFFD;
    uint32_t cp = first & (0x3F >> extra);
    for (int i = 0; i < extra; ++i) {
        if (pos >= text.size() || (byte(pos) & 0xC0) != 0x80) return 0xFFFD;
        cp = (cp << 6) | (byte(pos++) & 0x3F);
    }
    const bool overlong = (extra == 2 && cp < 0x800) || (extra == 3 && cp < 0x10000);
    if (overlong || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 0xFFFD;
    return cp;
}

struct SdfFont::Impl {
    stbtt_fontinfo fonts[kFontFaceCount] = {};
    float scale[kFontFaceCount] = {};             // font units to base pixels
    float ascent[kFontFaceCount] = {}, descent[kFontFaceCount] = {}, gap[kFontFaceCount] = {};   // base pixels
    std::vector<uint8_t> pixels = std::vector<uint8_t>(static_cast<size_t>(kAtlasSize) * kAtlasSize, 0);
    std::unordered_map<uint64_t, Glyph> glyphs;
    std::unordered_set<uint32_t> reported;
    uint32_t shelfX = 0, shelfY = 0, shelfHeight = 0;
    bool dirty = true;
    SdfFontStats stats;

    // A w x h rectangle of the atlas (1 texel apart from its neighbours); false when the page is full.
    bool place(uint32_t w, uint32_t h, uint32_t &x, uint32_t &y) {
        if (w + 1 > kAtlasSize || h + 1 > kAtlasSize) return false;
        if (shelfX + w + 1 > kAtlasSize) {
            shelfY += shelfHeight + 1;
            shelfX = 0;
            shelfHeight = 0;
        }
        if (shelfY + h + 1 > kAtlasSize) return false;
        x = shelfX + 1;
        y = shelfY + 1;
        shelfX += w + 1;
        shelfHeight = std::max(shelfHeight, h);
        return true;
    }

    void store(Glyph &g, const uint8_t *bitmap, int w, int h) {
        uint32_t x = 0, y = 0;
        if (!place(static_cast<uint32_t>(w), static_cast<uint32_t>(h), x, y)) {
            if (!stats.full)
                std::fprintf(stderr, "ui text: the glyph atlas (%u x %u) is full: later glyphs are boxes\n", kAtlasSize,
                             kAtlasSize);
            stats.full = true;
            g.visible = false;
            return;
        }
        for (int row = 0; row < h; ++row)
            std::memcpy(&pixels[(static_cast<size_t>(y) + row) * kAtlasSize + x], bitmap + static_cast<size_t>(row) * w,
                        static_cast<size_t>(w));
        const float size = static_cast<float>(kAtlasSize);
        g.u0 = static_cast<float>(x) / size;
        g.v0 = static_cast<float>(y) / size;
        g.u1 = static_cast<float>(x + static_cast<uint32_t>(w)) / size;
        g.v1 = static_cast<float>(y + static_cast<uint32_t>(h)) / size;
        g.w = static_cast<float>(w);
        g.h = static_cast<float>(h);
        g.visible = true;
        dirty = true;
        ++stats.glyphs;
    }

    // The box drawn for a codepoint no font has: a hollow rectangle, as a distance field (inside > kOnEdge).
    Glyph makeBox() {
        Glyph g;
        const float em = kBasePixels, half_w = 0.25f * em, half_h = 0.35f * em, stroke = 0.07f * em;
        const int pad = kSpread;
        const int w = static_cast<int>(std::ceil(2.0f * half_w)) + 2 * pad;
        const int h = static_cast<int>(std::ceil(2.0f * half_h)) + 2 * pad;
        std::vector<uint8_t> bitmap(static_cast<size_t>(w) * h);
        const auto box = [](float px, float py, float hx, float hy) {
            const float dx = std::fabs(px) - hx, dy = std::fabs(py) - hy;
            const float ox = std::max(dx, 0.0f), oy = std::max(dy, 0.0f);
            return std::sqrt(ox * ox + oy * oy) + std::min(std::max(dx, dy), 0.0f);
        };
        for (int row = 0; row < h; ++row)
            for (int col = 0; col < w; ++col) {
                const float px = static_cast<float>(col) + 0.5f - static_cast<float>(w) * 0.5f;
                const float py = static_cast<float>(row) + 0.5f - static_cast<float>(h) * 0.5f;
                const float outer = box(px, py, half_w, half_h), inner = box(px, py, half_w - stroke, half_h - stroke);
                const float outside = std::max(outer, -inner);
                const float value = static_cast<float>(kOnEdge) - outside * kDistanceScale;
                bitmap[static_cast<size_t>(row) * w + col] = static_cast<uint8_t>(std::clamp(value, 0.0f, 255.0f));
            }
        g.advance = 2.0f * half_w + 0.12f * em;
        g.xoff = 0.06f * em - static_cast<float>(pad);
        g.yoff = -2.0f * half_h - static_cast<float>(pad);
        store(g, bitmap.data(), w, h);
        return g;
    }

    const Glyph &glyph(FontFace face, uint32_t codepoint) {
        const auto found = glyphs.find(key(face, codepoint));
        if (found != glyphs.end()) return found->second;
        Glyph g;
        int font = -1, index = 0;
        if (codepoint != kBoxCodepoint) {
            index = stbtt_FindGlyphIndex(&fonts[static_cast<int>(face)], static_cast<int>(codepoint));
            if (index) font = static_cast<int>(face);
            for (FontFace other : kFallback) {
                if (font >= 0) break;
                index = stbtt_FindGlyphIndex(&fonts[static_cast<int>(other)], static_cast<int>(codepoint));
                if (index) font = static_cast<int>(other);
            }
        }
        if (font < 0) {
            // No font has it: the box (shared by every face), counted and logged once per codepoint.
            if (codepoint != kBoxCodepoint && reported.insert(codepoint).second) {
                ++stats.missing;
                std::fprintf(stderr, "ui text: U+%04X is in none of the UI fonts: drawn as a box\n", codepoint);
            }
            const auto box = glyphs.find(key(FontFace::Row, kBoxCodepoint));
            g = box != glyphs.end() ? box->second : (glyphs[key(FontFace::Row, kBoxCodepoint)] = makeBox());
            return glyphs[key(face, codepoint)] = g;
        }
        const stbtt_fontinfo &info = fonts[font];
        int advance = 0, lsb = 0;
        stbtt_GetGlyphHMetrics(&info, index, &advance, &lsb);
        g.advance = static_cast<float>(advance) * scale[font];
        g.index = index;
        g.font = font;
        int w = 0, h = 0, xoff = 0, yoff = 0;
        unsigned char *bitmap = stbtt_GetGlyphSDF(&info, scale[font], index, kSpread, kOnEdge, kDistanceScale, &w, &h,
                                                  &xoff, &yoff);
        if (bitmap) {
            g.xoff = static_cast<float>(xoff);
            g.yoff = static_cast<float>(yoff);
            store(g, bitmap, w, h);
            stbtt_FreeSDF(bitmap, nullptr);
        }
        return glyphs[key(face, codepoint)] = g;
    }

    float kern(const Glyph &previous, const Glyph &next) const {
        if (!previous.index || !next.index || previous.font != next.font) return 0.0f;
        return static_cast<float>(stbtt_GetGlyphKernAdvance(&fonts[next.font], previous.index, next.index)) *
               scale[next.font];
    }
};

SdfFont::SdfFont() : impl(std::make_unique<Impl>()) {
    const auto start = std::chrono::steady_clock::now();
    for (int face = 0; face < kFontFaceCount; ++face) {
        const EmbeddedFont *file = nullptr;
        for (size_t i = 0; i < kEmbeddedFontCount; ++i)
            if (std::strcmp(kEmbeddedFonts[i].name, kFaceFiles[face]) == 0) file = &kEmbeddedFonts[i];
        if (!file || !stbtt_InitFont(&impl->fonts[face], file->data, stbtt_GetFontOffsetForIndex(file->data, 0)))
            throw std::runtime_error(std::string("ui text: the font ") + kFaceFiles[face] +
                                     " is missing or unreadable");
        impl->scale[face] = stbtt_ScaleForMappingEmToPixels(&impl->fonts[face], kBasePixels);
        int ascent = 0, descent = 0, gap = 0;
        stbtt_GetFontVMetrics(&impl->fonts[face], &ascent, &descent, &gap);
        impl->ascent[face] = static_cast<float>(ascent) * impl->scale[face];
        impl->descent[face] = static_cast<float>(-descent) * impl->scale[face];
        impl->gap[face] = static_cast<float>(gap) * impl->scale[face];
    }
    impl->glyphs[key(FontFace::Row, kBoxCodepoint)] = impl->makeBox();
    // ASCII up front in every face; everything else when first drawn.
    for (int face = 0; face < kFontFaceCount; ++face)
        for (uint32_t cp = 32; cp < 127; ++cp) impl->glyph(static_cast<FontFace>(face), cp);
    impl->stats.atlasBytes = impl->pixels.size();
    impl->stats.buildMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

SdfFont::~SdfFont() = default;

float SdfFont::layout(FontFace face, float em, std::string_view text, float x, float baseline,
                      std::vector<GlyphQuad> &out) {
    const float k = em / kBasePixels;
    float pen = x;
    const Glyph *previous = nullptr;
    for (size_t pos = 0; pos < text.size();) {
        const uint32_t cp = nextCodepoint(text, pos);
        if (cp < 32) continue;   // control characters: callers break lines themselves
        const Glyph &g = impl->glyph(face, cp);
        if (previous) pen += impl->kern(*previous, g) * k;
        if (g.visible) {
            const float x0 = pen + g.xoff * k, y0 = baseline + g.yoff * k;
            out.push_back({x0, y0, x0 + g.w * k, y0 + g.h * k, g.u0, g.v0, g.u1, g.v1});
        }
        pen += g.advance * k;
        previous = &g;
    }
    return pen - x;
}

float SdfFont::measure(FontFace face, float em, std::string_view text) {
    const float k = em / kBasePixels;
    float width = 0.0f;
    const Glyph *previous = nullptr;
    for (size_t pos = 0; pos < text.size();) {
        const uint32_t cp = nextCodepoint(text, pos);
        if (cp < 32) continue;
        const Glyph &g = impl->glyph(face, cp);
        if (previous) width += impl->kern(*previous, g) * k;
        width += g.advance * k;
        previous = &g;
    }
    return width;
}

float SdfFont::ascent(FontFace face, float em) const {
    return impl->ascent[static_cast<int>(face)] * em / kBasePixels;
}

float SdfFont::lineHeight(FontFace face, float em) const {
    const int f = static_cast<int>(face);
    return (impl->ascent[f] + impl->descent[f] + impl->gap[f]) * em / kBasePixels;
}

const std::vector<uint8_t> &SdfFont::atlas() const { return impl->pixels; }

bool SdfFont::takeDirty() {
    const bool was = impl->dirty;
    impl->dirty = false;
    return was;
}

const SdfFontStats &SdfFont::stats() const { return impl->stats; }

}  // namespace gt4::ui
