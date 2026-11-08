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

// RGBA8 texels (R in the low byte, the GS's alpha: 0x80 = 1.0), level 0. One pass over at most 128 x 128 of them: a
// larger texture is sampled every step texels each way (it runs at every decode on the front-end thread; a race
// decodes about 129 textures a frame, some 512 x 256). The central disc is the texels within a quarter of the
// half-size of the centre, the rim those beyond three quarters (the corners count as rim too).
inline TextureSummary summarizeTexture(const uint32_t* rgba, uint32_t width, uint32_t height) {
    TextureSummary s;
    s.width = static_cast<uint16_t>(std::min<uint32_t>(width, 0xFFFF));
    s.height = static_cast<uint16_t>(std::min<uint32_t>(height, 0xFFFF));
    if (!rgba || !width || !height) return s;
    const uint32_t step = std::max(1u, (std::max(width, height) + 127) / 128);
    float sum[3] = {}, weight = 0.0f, centre = 0.0f, rim = 0.0f;
    uint32_t core = 0, samples = 0, centreCount = 0, rimCount = 0;
    const float cx = 0.5f * width, cy = 0.5f * height, half = 0.5f * static_cast<float>(std::min(width, height));
    for (uint32_t y = step / 2; y < height; y += step)
        for (uint32_t x = step / 2; x < width; x += step) {
            const uint32_t t = rgba[size_t(y) * width + x];
            const float r = static_cast<float>(t & 0xFF), g = static_cast<float>((t >> 8) & 0xFF), b = static_cast<float>((t >> 16) & 0xFF);
            const uint32_t a = t >> 24;
            const float w = static_cast<float>(std::min(a, 0x80u)) / 128.0f;
            sum[0] += r * w;
            sum[1] += g * w;
            sum[2] += b * w;
            weight += w;
            core += a == 0x80 ? 1 : 0;
            ++samples;
            const float dx = (x + 0.5f - cx) / half, dy = (y + 0.5f - cy) / half, d2 = dx * dx + dy * dy;
            const float value = (0.2126f * r + 0.7152f * g + 0.0722f * b) * w;
            if (d2 <= 0.25f * 0.25f) centre += value, ++centreCount;
            else if (d2 >= 0.75f * 0.75f) rim += value, ++rimCount;
        }
    for (int c = 0; c < 3; ++c) s.mean[c] = static_cast<uint8_t>(weight > 0.0f ? std::min(255.0f, sum[c] / weight + 0.5f) : 0.0f);
    s.alphaCore = static_cast<uint8_t>(samples ? std::min<uint32_t>(255, (core * 255 + samples / 2) / samples) : 0);
    const float c = centreCount ? centre / static_cast<float>(centreCount) : 0.0f, rr = rimCount ? rim / static_cast<float>(rimCount) : 0.0f;
    s.centreToRim = rr > 1e-3f ? c / rr : (c > 1e-3f ? 1e3f : 0.0f);
    s.known = 1;
    return s;
}

}  // namespace gt4::scene
