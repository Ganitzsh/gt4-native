#pragma once
// A texture's summary (PLAN.md F2b): 16 bytes the front end keeps per decoded texture, computed once from level 0 when
// it is decoded (the pixels are not kept after their upload), so the material classifier (lane F's glow class) can
// judge a texture at a material's first sighting without its pixels. Of the game's texture, never a pack's
// replacement, so the verdict does not depend on the pack.
#include <algorithm>
#include <cstdint>

namespace gt4::scene {

struct TextureSummary {
    uint8_t mean[3] = {};          // the mean colour, weighted by alpha (RGB, 0..255)
    uint8_t alphaCore = 0;         // the share of texels whose alpha is exactly 0x80 (opaque on the GS), 0..255
    float centreToRim = 0.0f;      // luminance x alpha in the central disc over the same on the rim (a glow: >> 1)
    uint16_t width = 0, height = 0;
    uint32_t known = 0;            // 1 when computed (0: no summary, e.g. a texture never decoded)
};
static_assert(sizeof(TextureSummary) == 16);

// RGBA8 texels (R in the low byte, the GS's alpha: 0x80 = 1.0), level 0. One pass over them. The central disc is
// the texels within a quarter of the half-size of the centre, the rim those beyond three quarters (inside the
// inscribed circle's square: the corners count as rim too).
inline TextureSummary summarizeTexture(const uint32_t* rgba, uint32_t width, uint32_t height) {
    TextureSummary s;
    s.width = static_cast<uint16_t>(std::min<uint32_t>(width, 0xFFFF));
    s.height = static_cast<uint16_t>(std::min<uint32_t>(height, 0xFFFF));
    if (!rgba || !width || !height) return s;
    double sum[3] = {}, weight = 0.0, centre = 0.0, rim = 0.0;
    uint64_t core = 0, centreCount = 0, rimCount = 0;
    const double cx = 0.5 * width, cy = 0.5 * height, half = 0.5 * std::min(width, height);
    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < width; ++x) {
            const uint32_t t = rgba[size_t(y) * width + x];
            const double r = t & 0xFF, g = (t >> 8) & 0xFF, b = (t >> 16) & 0xFF;
            const uint32_t a = t >> 24;
            const double w = std::min(a, 0x80u) / 128.0;
            sum[0] += r * w;
            sum[1] += g * w;
            sum[2] += b * w;
            weight += w;
            core += a == 0x80 ? 1 : 0;
            const double dx = (x + 0.5 - cx) / half, dy = (y + 0.5 - cy) / half, d2 = dx * dx + dy * dy;
            const double value = (0.2126 * r + 0.7152 * g + 0.0722 * b) * w;
            if (d2 <= 0.25 * 0.25) centre += value, ++centreCount;
            else if (d2 >= 0.75 * 0.75) rim += value, ++rimCount;
        }
    for (int c = 0; c < 3; ++c) s.mean[c] = static_cast<uint8_t>(weight > 0.0 ? std::min(255.0, sum[c] / weight + 0.5) : 0.0);
    s.alphaCore = static_cast<uint8_t>(std::min<uint64_t>(255, (core * 255 + width * height / 2) / (uint64_t(width) * height)));
    const double c = centreCount ? centre / double(centreCount) : 0.0, rr = rimCount ? rim / double(rimCount) : 0.0;
    s.centreToRim = static_cast<float>(rr > 1e-3 ? c / rr : (c > 1e-3 ? 1e3 : 0.0));
    s.known = 1;
    return s;
}

}  // namespace gt4::scene
