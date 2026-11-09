// The host UI's SDF text (platform/ui/sdf_font, PLAN.md E5): the embedded fonts load, layout and measure agree and
// scale with the size, UTF-8 decodes (malformed bytes as U+FFFD), a codepoint none of the fonts has is a box counted
// once, and the atlas reports new glyphs. Prints the build's cost (E5: reported in the commit).
#include "sdf_font.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const char *what) {
    if (!ok) {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}
}  // namespace

int main() {
    using gt4::ui::FontFace;
    using gt4::ui::GlyphQuad;
    using gt4::ui::nextCodepoint;

    // UTF-8
    {
        const std::string text = "A\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80";   // A, e-acute, euro, an emoji
        size_t pos = 0;
        check(nextCodepoint(text, pos) == 'A', "ASCII decodes");
        check(nextCodepoint(text, pos) == 0xE9, "a two-byte sequence decodes");
        check(nextCodepoint(text, pos) == 0x20AC, "a three-byte sequence decodes");
        check(nextCodepoint(text, pos) == 0x1F600 && pos == text.size(), "a four-byte sequence decodes");
        const std::string bad = "\xFF\xC0\x80\xE2\x82";
        pos = 0;
        check(nextCodepoint(bad, pos) == 0xFFFD, "a stray byte is U+FFFD");
        check(nextCodepoint(bad, pos) == 0xFFFD, "an overlong sequence is U+FFFD");
        pos = 3;
        check(nextCodepoint(bad, pos) == 0xFFFD && pos <= bad.size(), "a cut sequence is U+FFFD");
        const std::string leads = "\xF8\x80\x80\xFC\x80\x80\x80\x80\x80";   // five- and six-byte leads (lane C)
        pos = 0;
        check(nextCodepoint(leads, pos) == 0xFFFD && pos == 1, "an F8 lead is U+FFFD, not a three-byte character");
        pos = 3;
        check(nextCodepoint(leads, pos) == 0xFFFD && pos == 4, "an FC lead is U+FFFD");
    }

    gt4::ui::SdfFont font;
    const auto &stats = font.stats();
    std::printf("sdf font: %.1f ms to build, %d glyphs, atlas %zu bytes (%u x %u)\n", stats.buildMs, stats.glyphs,
                stats.atlasBytes, gt4::ui::SdfFont::kAtlasSize, gt4::ui::SdfFont::kAtlasSize);
    check(stats.glyphs >= 4 * 90, "ASCII is in the atlas for every face");
    check(stats.missing == 0 && !stats.full, "nothing missing at start");
    check(font.atlas().size() == static_cast<size_t>(gt4::ui::SdfFont::kAtlasSize) * gt4::ui::SdfFont::kAtlasSize,
          "the atlas is one page");
    check(font.takeDirty() && !font.takeDirty(), "the first upload is due, then nothing until a new glyph");

    // Layout and measure agree, and both scale with the size.
    for (FontFace face : {FontFace::Title, FontFace::Row, FontFace::Help, FontFace::Accent}) {
        std::vector<GlyphQuad> quads;
        const std::string text = "Render scale 100 %";
        const float width = font.layout(face, 32.0f, text, 10.0f, 50.0f, quads);
        check(std::fabs(width - font.measure(face, 32.0f, text)) < 0.01f, "layout and measure give one width");
        check(quads.size() == 15, "one quad per visible character (spaces have none)");
        check(std::fabs(font.measure(face, 64.0f, text) - 2.0f * width) < 0.05f, "the width scales with the size");
        check(font.lineHeight(face, 32.0f) > font.ascent(face, 32.0f) && font.ascent(face, 32.0f) > 10.0f,
              "line metrics are sane");
        bool inside = true;
        for (const auto &q : quads)
            inside = inside && q.x1 > q.x0 && q.y1 > q.y0 && q.u1 > q.u0 && q.v1 > q.v0 && q.u1 <= 1.0f && q.v1 <= 1.0f;
        check(inside, "quads and atlas rectangles are well formed");
    }

    // Latin-1 comes in on demand; a codepoint none of the fonts has is a box, counted once.
    {
        std::vector<GlyphQuad> quads;
        font.layout(FontFace::Row, 24.0f, "Caf\xC3\xA9", 0.0f, 0.0f, quads);   // Cafe with an acute e
        check(quads.size() == 4 && font.takeDirty(), "an accented letter is added to the atlas");
        check(font.stats().missing == 0, "Latin-1 is in the fonts");
        quads.clear();
        font.layout(FontFace::Help, 24.0f, "\xE4\xB8\xAD", 0.0f, 0.0f, quads);   // a CJK ideograph
        check(quads.size() == 1 && font.stats().missing == 1, "a codepoint no font has is drawn as a box, counted");
        font.layout(FontFace::Title, 24.0f, "\xE4\xB8\xAD", 0.0f, 0.0f, quads);
        check(font.stats().missing == 1, "and counted once");
    }

    if (failures) {
        std::printf("ui_sdf_font: %d check(s) failed\n", failures);
        return 1;
    }
    std::printf("ui_sdf_font: all checks passed\n");
    return 0;
}
