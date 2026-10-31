// PCSX2 texture names (see texture_name.h). The rules, from PCSX2's GSTextureReplacements and GSTextureCache
// (docs/TEXTURE_PACKS.md, "Names"): XXH3-64 (seed 0) of the texture's bytes level by level, either the GS
// memory blocks it covers as they are stored or its texels read out row by row; XXH3-64 of the palette
// expanded to 32-bit entries; PSM, TW, TH and (16 and 24-bit formats) TEXA in the low 32 bits.
#include "texture_name.h"

#define XXH_INLINE_ALL
#include "xxhash.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace gt4::gsn {

namespace {

struct Format {
    uint32_t blockW = 8, blockH = 8;   // texels per 256-byte block
    uint32_t entries = 0;              // palette entries (0: direct colour)
    bool whole = false;                // every bit of the stored texel is the texel: blocks hash as stored
};

bool formatOf(uint32_t psm, Format& f) {
    switch (psm) {
    case PSMCT32: case PSMZ32: f = {8, 8, 0, true}; return true;
    case PSMCT24: case PSMZ24: f = {8, 8, 0, false}; return true;
    case PSMCT16: case PSMCT16S: case PSMZ16: case PSMZ16S: f = {16, 8, 0, false}; return true;
    case PSMT8: f = {16, 16, 256, true}; return true;
    case PSMT4: f = {32, 16, 16, true}; return true;
    case PSMT8H: f = {8, 8, 256, false}; return true;
    case PSMT4HL: case PSMT4HH: f = {8, 8, 16, false}; return true;
    default: return false;
    }
}

bool sixteenBit(uint32_t psm) { return psm == PSMCT16 || psm == PSMCT16S || psm == PSMZ16 || psm == PSMZ16S; }
bool twentyFourBit(uint32_t psm) { return psm == PSMCT24 || psm == PSMZ24; }

// A texture region in texels, maxima exclusive; an axis without one is 0, 0.
struct Region {
    int minX = 0, maxX = 0, minY = 0, maxY = 0;
    bool hasX() const { return minX || maxX; }
    bool hasY() const { return minY || maxY; }
    uint32_t width() const { return hasX() ? static_cast<uint16_t>(maxX - minX) : 0; }
    uint32_t height() const { return hasY() ? static_cast<uint16_t>(maxY - minY) : 0; }
    bool operator==(const Region&) const = default;
};

// The region CLAMP's region modes sample (only where it is smaller than the texture, or reaches past it).
Region regionOf(const GsMemory::Tex0& t, const gfx::Sampler& s) {
    Region r;
    auto axis = [](uint32_t mode, uint32_t lo, uint32_t hi, uint32_t log2Size, int& from, int& to) {
        const uint32_t size = 1u << log2Size;
        if (mode == 2 && hi >= lo) {   // REGION_CLAMP [MIN, MAX]
            const uint32_t w = hi - lo + 1;
            if (w < size || hi >= size) from = static_cast<int>(lo), to = static_cast<int>(hi + 1);
        } else if (mode == 3 && lo) {  // REGION_REPEAT: MIN the mask, MAX the fixed bits
            const uint32_t w = (lo | hi) - hi + 1;
            if (w < size || (hi && w <= size)) from = static_cast<int>(hi), to = static_cast<int>((lo | hi) + 1);
        }
    };
    axis(s.wrapU, s.minU, s.maxU, t.tw, r.minX, r.maxX);
    axis(s.wrapV, s.minV, s.maxV, t.th, r.minY, r.maxY);
    return r;
}

// The 256-byte block holding texel (x, y), as an index into GS memory.
uint32_t blockOf(uint32_t psm, uint32_t tbp, uint32_t tbw, uint32_t x, uint32_t y) {
    switch (psm) {
    case PSMZ32: return (GsMemory::addressZ32(tbp, tbw, x, y) >> 8) & 16383;
    case PSMT8: return (GsMemory::address8(tbp, tbw, x, y) >> 8) & 16383;
    case PSMT4: return (GsMemory::address4(tbp, tbw, x, y) >> 9) & 16383;
    default: return (GsMemory::address32(tbp, tbw, x, y) >> 8) & 16383;
    }
}

// A direct-colour texel as 32-bit RGBA (alpha as stored, or from TEXA).
uint32_t rgba(uint32_t psm, uint32_t raw, const Texa& texa) {
    if (twentyFourBit(psm)) {
        const uint32_t rgb = raw & 0xFFFFFF;
        return rgb | (static_cast<uint32_t>(texa.aem && !rgb ? 0 : texa.ta0) << 24);
    }
    if (sixteenBit(psm)) {
        const uint32_t c = raw & 0xFFFF;
        const uint32_t r = (c & 31) << 3, g = ((c >> 5) & 31) << 3, b = ((c >> 10) & 31) << 3;
        const uint32_t a = (c & 0x8000) ? texa.ta1 : (texa.aem && !(c & 0x7FFF) ? 0 : texa.ta0);
        return r | (g << 8) | (b << 16) | (a << 24);
    }
    return raw;
}

// One level of the texture into the hash: its blocks as stored when it is at least a block in size, all of
// its bits are texel and no region needs part of a block row; else its texels (palette indices, or RGBA),
// read out row by row. False for a region with no texels.
bool hashLevel(XXH3_state_t* state, const GsMemory& mem, const GsMemory::Tex0& t, const Texa& texa, const Region& region,
               std::vector<uint8_t>& scratch) {
    Format f;
    if (!formatOf(t.psm, f)) return false;
    const int tw = region.hasX() ? region.maxX - region.minX : 1 << t.tw;
    const int th = region.hasY() ? region.maxY - region.minY : 1 << t.th;
    if (tw <= 0 || th <= 0) return false;
    const int left = region.hasX() ? region.minX : 0, top = region.hasY() ? region.minY : 0;
    const int right = region.hasX() ? region.maxX : tw, bottom = region.hasY() ? region.maxY : th;
    const int bw = static_cast<int>(f.blockW), bh = static_cast<int>(f.blockH);
    auto down = [](int v, int m) { return v >= 0 ? v / m * m : -((-v + m - 1) / m) * m; };
    const int bl = down(left, bw), bt = down(top, bh), br = down(right + bw - 1, bw), bb = down(bottom + bh - 1, bh);
    const bool texels = tw < bw || th < bh || !f.whole || region.maxX > 0 || region.minY > 0;
    if (!texels) {
        for (int by = bt / bh; by < bb / bh; ++by)
            for (int bx = bl / bw; bx < br / bw; ++bx) {
                const uint32_t b = blockOf(t.psm, t.tbp, t.tbw, static_cast<uint32_t>(bx * bw), static_cast<uint32_t>(by * bh));
                XXH3_64bits_update(state, mem.data() + size_t(b) * 256, 256);
            }
        return true;
    }
    const size_t bytes = f.entries ? 1 : 4;
    const int width = br - bl, height = bb - bt;
    scratch.resize(size_t(width) * height * bytes);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const uint32_t raw = mem.readPixel(t.psm, t.tbp, t.tbw, static_cast<uint32_t>(bl + x), static_cast<uint32_t>(bt + y));
            uint8_t* out = scratch.data() + (size_t(y) * width + x) * bytes;
            if (f.entries) {
                *out = static_cast<uint8_t>(raw);
            } else {
                const uint32_t c = rgba(t.psm, raw, texa);
                std::memcpy(out, &c, 4);
            }
        }
    // (As PCSX2: a row starts `left - bl` bytes in, not texels, which differs for RGBA with an unaligned region.)
    const size_t x0 = static_cast<size_t>(left - bl), y0 = static_cast<size_t>(top - bt);
    for (int y = 0; y < th; ++y)
        XXH3_64bits_update(state, scratch.data() + (y0 + size_t(y)) * size_t(width) * bytes + x0, size_t(tw) * bytes);
    return true;
}

// The palette as 32-bit entries for the draw (CSA, CPSM, TEXA for 16-bit entries), hashed.
uint64_t clutHash(const GsMemory& mem, const GsMemory::Tex0& t, const Texa& texa, uint32_t entries) {
    const uint8_t* bytes = mem.clutBuffer();
    auto half = [&](uint32_t i) -> uint32_t {   // halfwords past the 1 KiB buffer read as 0
        if (i >= 512) return 0;
        uint16_t v;
        std::memcpy(&v, bytes + i * 2, 2);
        return v;
    };
    uint32_t palette[256];
    if (t.cpsm == PSMCT32 || t.cpsm == PSMCT24) {
        const uint32_t offset = (t.csa & 15) * 16;
        if (entries == 256) {
            for (uint32_t g = 0; g < 256; g += 16) {
                const uint32_t base = std::min<uint32_t>(g + offset, 240);
                for (uint32_t j = 0; j < 16; ++j) palette[g + j] = half(base + j) | (half(base + j + 256) << 16);
            }
        } else {
            for (uint32_t j = 0; j < 16; ++j) palette[j] = half(offset + j) | (half(offset + j + 256) << 16);
        }
    } else {
        for (uint32_t i = 0; i < entries; ++i) {
            const uint32_t c = half(t.csa * 16 + i);
            const uint32_t r = (c & 31) << 3, g = ((c >> 5) & 31) << 3, b = ((c >> 10) & 31) << 3;
            uint32_t a = (c & 0x8000) ? texa.ta1 : texa.ta0;
            if (texa.aem && !c) a = 0;
            palette[i] = r | (g << 8) | (b << 16) | (a << 24);
        }
    }
    return XXH3_64bits(palette, size_t(entries) * 4);
}

}  // namespace

bool findTextureName(const GsMemory& mem, const GsMemory::Tex0& tex0, const Texa& texa, const gfx::Sampler& sampler,
                     const MipAddress* mips, uint32_t mipCount, const TexturePack& pack, TextureName& name,
                     uint32_t origin[2]) {
    Format f;
    if (!formatOf(tex0.psm, f)) return false;
    TextureName key;
    key.bits = tex0.psm | (tex0.tw << 6) | (tex0.th << 10);
    if (!f.entries && (twentyFourBit(tex0.psm) || sixteenBit(tex0.psm)))
        key.bits |= (uint32_t(texa.ta0) << 15) | (uint32_t(texa.aem ? 1 : 0) << 23) | (uint32_t(texa.ta1) << 24);
    const std::vector<std::pair<uint32_t, uint32_t>>* sizes = pack.regionsFor(key.bits);
    if (!sizes) return false;
    // The regions to try: CLAMP's, none, and each size the pack has for these bits from the texture's corner
    // (what PCSX2 makes of a texture partly sampled after a partial upload).
    std::vector<Region> regions;
    auto add = [&](const Region& r) {
        if (std::find(regions.begin(), regions.end(), r) != regions.end()) return;
        if (std::find(sizes->begin(), sizes->end(), std::make_pair(r.width(), r.height())) == sizes->end()) return;
        if ((tex0.tw > 10 && !r.hasX()) || (tex0.th > 10 && !r.hasY())) return;
        regions.push_back(r);
    };
    add(regionOf(tex0, sampler));
    add(Region{});
    for (const auto& [w, h] : *sizes) add(Region{0, static_cast<int>(w), 0, static_cast<int>(h)});
    if (regions.empty()) return false;
    if (f.entries) key.clutHash = clutHash(mem, tex0, texa, f.entries);
    // A mipmapped draw hashes its levels after the first as well (from LOD 0: GT4's near textures).
    const bool mipDraw = sampler.mipMax > 0 && sampler.minFilter >= 2 && sampler.minFilter <= 5 && mipCount > 0;
    std::vector<uint8_t> scratch;
    XXH3_state_t state, chain;
    for (const Region& r : regions) {
        XXH3_64bits_reset(&state);
        if (!hashLevel(&state, mem, tex0, texa, r, scratch)) continue;
        key.regionWidth = r.width();
        key.regionHeight = r.height();
        if (mipDraw) {
            XXH3_copyState(&chain, &state);
            bool complete = true;
            for (uint32_t i = 1; i <= mipCount && complete; ++i) {
                GsMemory::Tex0 level = tex0;
                level.tbp = mips[i - 1].tbp;
                level.tbw = mips[i - 1].tbw;
                level.tw = tex0.tw > i ? tex0.tw - i : 0;
                level.th = tex0.th > i ? tex0.th - i : 0;
                Region lr = r;
                if (r.hasX()) {
                    lr.minX = r.minX >> i;
                    lr.maxX = lr.minX + std::max((r.maxX - r.minX) >> i, 1);
                }
                if (r.hasY()) {
                    lr.minY = r.minY >> i;
                    lr.maxY = lr.minY + std::max((r.maxY - r.minY) >> i, 1);
                }
                complete = hashLevel(&chain, mem, level, texa, lr, scratch);
            }
            key.texHash = XXH3_64bits_digest(&chain);
            if (complete && pack.has(key)) {
                name = key;
                origin[0] = r.hasX() ? static_cast<uint32_t>(r.minX) : 0;
                origin[1] = r.hasY() ? static_cast<uint32_t>(r.minY) : 0;
                return true;
            }
        }
        key.texHash = XXH3_64bits_digest(&state);
        if (pack.has(key)) {
            name = key;
            origin[0] = r.hasX() ? static_cast<uint32_t>(r.minX) : 0;
            origin[1] = r.hasY() ? static_cast<uint32_t>(r.minY) : 0;
            return true;
        }
    }
    return false;
}

}  // namespace gt4::gsn
