// The native graphics interface between GT4's graphics library and the renderer (docs/GRAPHICS_API_PLAN.md).
//
// The game describes what it draws with these types instead of GS register values: the drawing state of
// each of the two drawing contexts, the state they share, and records that carry them to the host. Both
// sides include this header: the game's runtime (32-bit MinGW) writes records, the host (64-bit MSVC)
// applies them; every type is plain data of fixed layout.
//
// What stays of the PS2's model is what GT4's pictures depend on: surfaces are named by their address in a
// 4 MiB graphics memory (in 256-byte blocks) with a pixel format, because the game reads a texture where it
// drew a target, through another format, with palettes it drew; blending is (A - B) * C + D with alpha
// 0x80 = 1.0.
//
// The state is grouped as GT4's graphics library keeps it (its shadow at 0x70002000 + 0x120 and + 0x170,
// with one dirty bit per group): a state record carries the groups that changed, applied in bit order.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace gt4::gfx {

// Pixel formats (their numbers are the GS's).
enum Format : uint8_t {
    C32 = 0x00, C24 = 0x01, C16 = 0x02, C16S = 0x0A,
    T8 = 0x13, T4 = 0x14, T8H = 0x1B, T4HL = 0x24, T4HH = 0x2C,
    Z32 = 0x30, Z24 = 0x31, Z16 = 0x32, Z16S = 0x3A,
};

enum Compare : uint8_t { Never = 0, Always = 1 };   // alpha: 2 LESS .. 7 NOTEQUAL; depth: 2 GEQUAL, 3 GREATER

// Primitive attributes (a draw's kind and how it is shaded).
enum PrimType : uint32_t { Points = 0, Lines = 1, LineStrip = 2, Triangles = 3, TriStrip = 4, TriFan = 5, Sprites = 6 };
enum PrimFlags : uint32_t {
    kPrimType = 7, kGouraud = 1u << 3, kTextured = 1u << 4, kFog = 1u << 5, kBlend = 1u << 6, kAntialias = 1u << 7,
    kTexelCoords = 1u << 8, kContext2 = 1u << 9, kFixedFragment = 1u << 10,
};

struct Texture {
    uint16_t base = 0;            // blocks
    uint8_t width = 0;            // 64-texel units
    uint8_t format = C32;
    uint8_t log2Width = 0, log2Height = 0;
    uint8_t useAlpha = 0;         // RGBA texels (0: RGB, alpha from the vertex)
    uint8_t function = 0;         // 0 modulate, 1 decal, 2 highlight, 3 highlight2
    uint16_t clutBase = 0;        // blocks
    uint8_t clutFormat = C32;
    uint8_t clutStorage = 0;      // 0: CSM1 (swizzled 16 x 16), 1: CSM2 (a line)
    uint8_t clutOffset = 0;       // in 16-entry units
    uint8_t clutLoad = 0;         // when this texture is set: 0 keep the palette, 1 load, 2/3 load and remember the base,
                                  // 4/5 load if the base differs from the remembered one
    // Native (docs/NATIVE_V2.md): a texture of a texture set (TexSetRecord): the set's id and the record's index;
    // set 0: the texture is named by its graphics-memory fields above.
    uint16_t record = 0;
    uint32_t set = 0;
    bool operator==(const Texture&) const = default;
};

struct Sampler {
    uint8_t wrapU = 0, wrapV = 0;           // 0 repeat, 1 clamp, 2 region clamp, 3 region repeat
    uint16_t minU = 0, maxU = 0, minV = 0, maxV = 0;
    uint8_t lodFixed = 0;                    // 1: LOD = K; 0: from Q
    uint8_t mipMax = 0;                      // levels beyond the first (0..6)
    uint8_t magLinear = 0;
    uint8_t minFilter = 0;                   // 0 nearest, 1 linear, 2..5 the mipmap modes
    uint8_t mipAuto = 0;                     // levels 1-3 follow level 0 in memory
    uint8_t lodL = 0;
    int16_t lodK = 0;                        // 1/16 units
    uint16_t mipBase[6] = {};                // levels 1..6
    uint8_t mipWidth[6] = {};
    bool operator==(const Sampler&) const = default;
};

// One drawing context.
struct Context {
    // Where it draws.
    uint16_t colorBase = 0;      // blocks
    uint8_t colorWidth = 0;      // 64-pixel units
    uint8_t colorFormat = C32;
    uint32_t colorKeep = 0;      // bits of each pixel left as they are
    uint16_t depthBase = 0;
    uint8_t depthFormat = Z32;
    uint8_t depthWrite = 1;
    uint16_t originX = 0, originY = 0;   // the window's (0, 0) in vertex coordinates, 1/16 pixel
    uint16_t scissorX0 = 0, scissorX1 = 0, scissorY0 = 0, scissorY1 = 0;   // inclusive
    uint8_t forceAlpha = 0;      // the written alpha's top bit set
    // The blend: (A - B) * C + D; A, B, D: 0 source, 1 destination, 2 zero; C: 0 source alpha, 1 destination
    // alpha, 2 the fixed value.
    uint8_t blendA = 0, blendB = 0, blendC = 0, blendD = 0, blendFixed = 0;
    // Tests.
    uint8_t alphaTest = 0, alphaFunc = 0, alphaRef = 0;
    uint8_t alphaFail = 0;       // what a failing pixel still writes: 0 nothing, 1 colour, 2 depth, 3 RGB
    uint8_t destTest = 0;        // destination alpha test
    uint8_t destPass = 0;        // the pixels it passes: 0 alpha bit clear, 1 set
    uint8_t depthTest = 0, depthFunc = 0;
    Texture texture;
    Sampler sampler;
    bool operator==(const Context&) const = default;
};

// State both contexts share.
struct Shared {
    uint8_t colorClamp = 1;      // 0: colours wrap
    uint8_t dither = 0;
    uint8_t perPixelBlend = 0;   // blend only pixels whose alpha's top bit is set
    uint8_t attributesFromPrim = 1;   // 0: attributes from the PRMODE value
    uint32_t fogColor = 0;
    uint8_t texAlpha0 = 0, texAlpha1 = 0, texBlackTransparent = 0;   // alpha of 16/24-bit texels (0x80 = 1)
    uint8_t scanMask = 0;
    uint64_t ditherMatrix = 0;
    uint16_t clutWidth = 0, clutU = 0, clutV = 0;   // CSM2 palettes
    bool operator==(const Shared&) const = default;
};

// The groups, in the order the library flushes them. Context groups (bits 0..12 for context 1, 16..28 for
// context 2), applied with TEX2 before TEX1 before TEX0:
enum ContextGroup : uint32_t {
    kFrame = 0, kOrigin = 1, kForceAlpha = 2, kDepthBuffer = 3, kScissor = 4, kBlendGroup = 5, kTests = 6,
    kWrap = 7, kPalette = 8 /* TEX2: the texture's format and palette */, kFilter = 9, kTexture = 10,
    kMip1 = 11, kMip2 = 12, kContextGroups = 13,
};
enum SharedGroup : uint32_t {
    kColorClamp = 0, kDitherMatrix = 1, kDither = 2, kFogColor = 3, kPerPixelBlend = 4, kAttributes = 5,
    kScanMask = 6, kTexAlpha = 7, kClutLayout = 8, kTextureFlush = 9, kSharedGroups = 10,
};

// The GS register each group came from (debugging, and the reference GIF path).
inline constexpr uint8_t kContextRegister[kContextGroups] = {0x4C, 0x18, 0x4A, 0x4E, 0x40, 0x42, 0x47, 0x08, 0x16, 0x14, 0x06, 0x34, 0x36};
inline constexpr uint8_t kSharedRegister[kSharedGroups] = {0x46, 0x44, 0x45, 0x3D, 0x49, 0x1A, 0x22, 0x3B, 0x1C, 0x3F};

// --- GT4's library keeps its shadow in GS register form: the groups from those values -------------------

inline void setTexture(Texture& t, uint64_t v) {
    t.base = static_cast<uint16_t>(v & 0x3FFF);
    t.width = static_cast<uint8_t>((v >> 14) & 0x3F);
    t.format = static_cast<uint8_t>((v >> 20) & 0x3F);
    t.log2Width = static_cast<uint8_t>((v >> 26) & 0xF);
    t.log2Height = static_cast<uint8_t>((v >> 30) & 0xF);
    t.useAlpha = static_cast<uint8_t>((v >> 34) & 1);
    t.function = static_cast<uint8_t>((v >> 35) & 3);
    t.clutBase = static_cast<uint16_t>((v >> 37) & 0x3FFF);
    t.clutFormat = static_cast<uint8_t>((v >> 51) & 0xF);
    t.clutStorage = static_cast<uint8_t>((v >> 55) & 1);
    t.clutOffset = static_cast<uint8_t>((v >> 56) & 0x1F);
    t.clutLoad = static_cast<uint8_t>((v >> 61) & 7);
    t.record = 0;   // a texture by its graphics-memory fields (the bind of a set's record names it after)
    t.set = 0;
}

// TEX2: the format and palette fields only.
inline void setPalette(Texture& t, uint64_t v) {
    t.format = static_cast<uint8_t>((v >> 20) & 0x3F);
    t.clutBase = static_cast<uint16_t>((v >> 37) & 0x3FFF);
    t.clutFormat = static_cast<uint8_t>((v >> 51) & 0xF);
    t.clutStorage = static_cast<uint8_t>((v >> 55) & 1);
    t.clutOffset = static_cast<uint8_t>((v >> 56) & 0x1F);
    t.clutLoad = static_cast<uint8_t>((v >> 61) & 7);
    t.record = 0;   // a texture by its graphics-memory fields (the bind of a set's record names it after)
    t.set = 0;
}

// Sets one group of `c` from its register value; false for a group this does not know.
inline bool setContextGroup(Context& c, uint32_t group, uint64_t v) {
    switch (group) {
    case kFrame:
        c.colorBase = static_cast<uint16_t>((v & 0x1FF) * 32);
        c.colorWidth = static_cast<uint8_t>((v >> 16) & 0x3F);
        c.colorFormat = static_cast<uint8_t>((v >> 24) & 0x3F);
        c.colorKeep = static_cast<uint32_t>(v >> 32);
        return true;
    case kOrigin:
        c.originX = static_cast<uint16_t>(v & 0xFFFF);
        c.originY = static_cast<uint16_t>((v >> 32) & 0xFFFF);
        return true;
    case kForceAlpha: c.forceAlpha = static_cast<uint8_t>(v & 1); return true;
    case kDepthBuffer:
        c.depthBase = static_cast<uint16_t>((v & 0x1FF) * 32);
        c.depthFormat = static_cast<uint8_t>(((v >> 24) & 0xF) | 0x30);
        c.depthWrite = !((v >> 32) & 1);
        return true;
    case kScissor:
        c.scissorX0 = static_cast<uint16_t>(v & 0x7FF);
        c.scissorX1 = static_cast<uint16_t>((v >> 16) & 0x7FF);
        c.scissorY0 = static_cast<uint16_t>((v >> 32) & 0x7FF);
        c.scissorY1 = static_cast<uint16_t>((v >> 48) & 0x7FF);
        return true;
    case kBlendGroup:
        c.blendA = static_cast<uint8_t>(v & 3);
        c.blendB = static_cast<uint8_t>((v >> 2) & 3);
        c.blendC = static_cast<uint8_t>((v >> 4) & 3);
        c.blendD = static_cast<uint8_t>((v >> 6) & 3);
        c.blendFixed = static_cast<uint8_t>((v >> 32) & 0xFF);
        return true;
    case kTests:
        c.alphaTest = static_cast<uint8_t>(v & 1);
        c.alphaFunc = static_cast<uint8_t>((v >> 1) & 7);
        c.alphaRef = static_cast<uint8_t>((v >> 4) & 0xFF);
        c.alphaFail = static_cast<uint8_t>((v >> 12) & 3);
        c.destTest = static_cast<uint8_t>((v >> 14) & 1);
        c.destPass = static_cast<uint8_t>((v >> 15) & 1);
        c.depthTest = static_cast<uint8_t>((v >> 16) & 1);
        c.depthFunc = static_cast<uint8_t>((v >> 17) & 3);
        return true;
    case kWrap:
        c.sampler.wrapU = static_cast<uint8_t>(v & 3);
        c.sampler.wrapV = static_cast<uint8_t>((v >> 2) & 3);
        c.sampler.minU = static_cast<uint16_t>((v >> 4) & 0x3FF);
        c.sampler.maxU = static_cast<uint16_t>((v >> 14) & 0x3FF);
        c.sampler.minV = static_cast<uint16_t>((v >> 24) & 0x3FF);
        c.sampler.maxV = static_cast<uint16_t>((v >> 34) & 0x3FF);
        return true;
    case kPalette: setPalette(c.texture, v); return true;
    case kFilter: {
        Sampler& s = c.sampler;
        s.lodFixed = static_cast<uint8_t>(v & 1);
        s.mipMax = static_cast<uint8_t>((v >> 2) & 7);
        s.magLinear = static_cast<uint8_t>((v >> 5) & 1);
        s.minFilter = static_cast<uint8_t>((v >> 6) & 7);
        s.mipAuto = static_cast<uint8_t>((v >> 9) & 1);
        s.lodL = static_cast<uint8_t>((v >> 19) & 3);
        s.lodK = static_cast<int16_t>(static_cast<int32_t>(static_cast<uint32_t>(v >> 32) << 20) >> 20);   // 12-bit signed
        return true;
    }
    case kTexture: setTexture(c.texture, v); return true;
    case kMip1: case kMip2:
        for (uint32_t k = 0; k < 3; ++k) {
            const uint32_t level = (group == kMip1 ? 0 : 3) + k;
            c.sampler.mipBase[level] = static_cast<uint16_t>((v >> (20 * k)) & 0x3FFF);
            c.sampler.mipWidth[level] = static_cast<uint8_t>((v >> (20 * k + 14)) & 0x3F);
        }
        return true;
    }
    return false;
}

inline bool setSharedGroup(Shared& s, uint32_t group, uint64_t v) {
    switch (group) {
    case kColorClamp: s.colorClamp = static_cast<uint8_t>(v & 1); return true;
    case kDitherMatrix: s.ditherMatrix = v; return true;
    case kDither: s.dither = static_cast<uint8_t>(v & 1); return true;
    case kFogColor: s.fogColor = static_cast<uint32_t>(v & 0xFFFFFF); return true;
    case kPerPixelBlend: s.perPixelBlend = static_cast<uint8_t>(v & 1); return true;
    case kAttributes: s.attributesFromPrim = static_cast<uint8_t>(v & 1); return true;
    case kScanMask: s.scanMask = static_cast<uint8_t>(v & 3); return true;
    case kTexAlpha:
        s.texAlpha0 = static_cast<uint8_t>(v & 0xFF);
        s.texBlackTransparent = static_cast<uint8_t>((v >> 15) & 1);
        s.texAlpha1 = static_cast<uint8_t>((v >> 32) & 0xFF);
        return true;
    case kClutLayout:
        s.clutWidth = static_cast<uint16_t>(v & 0x3F);
        s.clutU = static_cast<uint16_t>((v >> 6) & 0x3F);
        s.clutV = static_cast<uint16_t>((v >> 12) & 0x3FF);
        return true;
    case kTextureFlush: return true;   // nothing to hold
    }
    return false;
}

// The group a GS register address sets: context 0/1 and the group, or the shared group; false otherwise.
inline bool groupOfRegister(uint32_t reg, bool& shared, uint32_t& context, uint32_t& group) {
    for (uint32_t g = 0; g < kContextGroups; ++g)
        if (reg == kContextRegister[g] || reg == kContextRegister[g] + 1u) {
            shared = false;
            context = reg - kContextRegister[g];
            group = g;
            return true;
        }
    for (uint32_t g = 0; g < kSharedGroups; ++g)
        if (reg == kSharedRegister[g]) {
            shared = true;
            context = 0;
            group = g;
            return true;
        }
    return false;
}

// --- Records ---------------------------------------------------------------------------------------------
//
// Records travel in the game's VIF1 stream while the rest of the stream is still packets (so they stay in
// order with them): a VIF code the hardware does not define, kStreamCode | qwords, in the last word of a DMA
// tag's upper half like DIRECT, followed by that many qwords holding one or more records. Each record is a
// Header and its payload, padded to 16 bytes.

constexpr uint32_t kStreamCode = 0x08000000u;   // VIF command 0x08

enum RecordType : uint32_t {
    kNop = 0,
    kState = 1,    // StateRecord
    kUpload = 2,   // UploadRecord
    kDraw = 3,     // DrawRecord, then its vertices
    kRead = 4,     // ReadRecord
    // GT4's 3D batches (docs/VU1_PROGRAMS.md "The batch"): the host keeps two batch buffers that take turns, as
    // the game fills them; a batch's renderer reads the one just filled.
    kBatchFrame = 5,   // no payload: a frame starts (the buffers' turn back to the first, the data layout reset)
    kBatchWrite = 6,   // BatchWrite, then its qwords
    kBatchModel = 7,   // BatchModel
    kBatchRun = 8,     // BatchRun
    kTexSet = 9,       // TexSetRecord, its uploads, its texture records (docs/NATIVE_V2.md)
};

struct Header {
    uint32_t word;   // the type in the low 8 bits, the payload's bytes after the header above
    uint32_t type() const { return word & 0xFF; }
    uint32_t bytes() const { return word >> 8; }
};
constexpr Header header(uint32_t type, uint32_t bytes) { return Header{type | bytes << 8}; }

// The groups that changed (shared groups, then context 1's, then context 2's), from one source context.
struct StateRecord {
    uint32_t sharedGroups;    // bits of SharedGroup
    uint32_t contextGroups;   // bits 0..12 context 1, 16..28 context 2 (ContextGroup)
    Shared shared;
    Context context;
    Texture palette;          // the palette group's fields (TEX2: format and palette), apart from context.texture (TEX0)
};
// One layout for the 32-bit runtime and the 64-bit host.
static_assert(sizeof(Texture) == 20 && sizeof(Context) == 96 && sizeof(Shared) == 32);
static_assert(sizeof(StateRecord) == 160 && offsetof(StateRecord, context) == 40 && offsetof(StateRecord, palette) == 136);

// Copies one group's fields.
inline void copySharedGroup(Shared& to, const Shared& from, uint32_t group) {
    switch (group) {
    case kColorClamp: to.colorClamp = from.colorClamp; break;
    case kDitherMatrix: to.ditherMatrix = from.ditherMatrix; break;
    case kDither: to.dither = from.dither; break;
    case kFogColor: to.fogColor = from.fogColor; break;
    case kPerPixelBlend: to.perPixelBlend = from.perPixelBlend; break;
    case kAttributes: to.attributesFromPrim = from.attributesFromPrim; break;
    case kScanMask: to.scanMask = from.scanMask; break;
    case kTexAlpha:
        to.texAlpha0 = from.texAlpha0;
        to.texAlpha1 = from.texAlpha1;
        to.texBlackTransparent = from.texBlackTransparent;
        break;
    case kClutLayout:
        to.clutWidth = from.clutWidth;
        to.clutU = from.clutU;
        to.clutV = from.clutV;
        break;
    default: break;
    }
}

inline void copyContextGroup(Context& to, const Context& from, const Texture& palette, uint32_t group) {
    switch (group) {
    case kFrame:
        to.colorBase = from.colorBase;
        to.colorWidth = from.colorWidth;
        to.colorFormat = from.colorFormat;
        to.colorKeep = from.colorKeep;
        break;
    case kOrigin:
        to.originX = from.originX;
        to.originY = from.originY;
        break;
    case kForceAlpha: to.forceAlpha = from.forceAlpha; break;
    case kDepthBuffer:
        to.depthBase = from.depthBase;
        to.depthFormat = from.depthFormat;
        to.depthWrite = from.depthWrite;
        break;
    case kScissor:
        to.scissorX0 = from.scissorX0;
        to.scissorX1 = from.scissorX1;
        to.scissorY0 = from.scissorY0;
        to.scissorY1 = from.scissorY1;
        break;
    case kBlendGroup:
        to.blendA = from.blendA;
        to.blendB = from.blendB;
        to.blendC = from.blendC;
        to.blendD = from.blendD;
        to.blendFixed = from.blendFixed;
        break;
    case kTests:
        to.alphaTest = from.alphaTest;
        to.alphaFunc = from.alphaFunc;
        to.alphaRef = from.alphaRef;
        to.alphaFail = from.alphaFail;
        to.destTest = from.destTest;
        to.destPass = from.destPass;
        to.depthTest = from.depthTest;
        to.depthFunc = from.depthFunc;
        break;
    case kWrap:
        to.sampler.wrapU = from.sampler.wrapU;
        to.sampler.wrapV = from.sampler.wrapV;
        to.sampler.minU = from.sampler.minU;
        to.sampler.maxU = from.sampler.maxU;
        to.sampler.minV = from.sampler.minV;
        to.sampler.maxV = from.sampler.maxV;
        break;
    case kPalette:
        to.texture.format = palette.format;
        to.texture.clutBase = palette.clutBase;
        to.texture.clutFormat = palette.clutFormat;
        to.texture.clutStorage = palette.clutStorage;
        to.texture.clutOffset = palette.clutOffset;
        to.texture.clutLoad = palette.clutLoad;
        break;
    case kFilter:
        to.sampler.lodFixed = from.sampler.lodFixed;
        to.sampler.mipMax = from.sampler.mipMax;
        to.sampler.magLinear = from.sampler.magLinear;
        to.sampler.minFilter = from.sampler.minFilter;
        to.sampler.mipAuto = from.sampler.mipAuto;
        to.sampler.lodL = from.sampler.lodL;
        to.sampler.lodK = from.sampler.lodK;
        break;
    case kTexture: to.texture = from.texture; break;   // (with its native set and record)
    case kMip1: case kMip2:
        for (uint32_t k = 0; k < 3; ++k) {
            const uint32_t level = (group == kMip1 ? 0 : 3) + k;
            to.sampler.mipBase[level] = from.sampler.mipBase[level];
            to.sampler.mipWidth[level] = from.sampler.mipWidth[level];
        }
        break;
    default: break;
    }
}

// Applies a state record: the shared groups, then context 1's and context 2's in bit order; `groupSet(context,
// group)` after each context group (the receiver's side effects: a texture set loads its palette).
template <class F>
void applyState(Shared& shared, Context (&contexts)[2], const StateRecord& r, F&& groupSet) {
    for (uint32_t g = 0; g < kSharedGroups; ++g)
        if ((r.sharedGroups >> g) & 1) copySharedGroup(shared, r.shared, g);
    for (uint32_t context = 0; context < 2; ++context)
        for (uint32_t g = 0; g < kContextGroups; ++g)
            if ((r.contextGroups >> (16 * context + g)) & 1) {
                copyContextGroup(contexts[context], r.context, r.palette, g);
                groupSet(context, g);
            }
}

// Pixels from the game's memory into graphics memory: a w x h rectangle at (x, y) of the surface at `base`
// (blocks), `width` (64-pixel units), in `format`; `qwords` x 16 bytes of packed pixels at `source` (a game
// address). The game's runtime sends those bytes ahead of the record (GT4_REC_GFX_DATA): the host takes them
// when it reaches the record.
struct UploadRecord {
    uint16_t base;
    uint8_t width, format;
    uint16_t x, y, w, h;
    uint32_t source;
    uint32_t qwords;
};
static_assert(sizeof(UploadRecord) == 20);

// Pixels from graphics memory into the game's memory: a w x h rectangle at (x, y) of the surface at `base`,
// `width`, in `format`, packed (`qwords` x 16 bytes) to `destination` (a game address). What was drawn before
// the record is in it. The game's runtime asks the host for the pixels when it reaches the record.
struct ReadRecord {
    uint16_t base;
    uint8_t width, format;
    uint16_t x, y, w, h;
    uint32_t destination;
    uint32_t qwords;
};
static_assert(sizeof(ReadRecord) == 20);

// Qwords into the batch being filled, from `qword` (0..255) on: the lanes `lanes` says (bit k: lane k) of each.
struct BatchWrite {
    uint16_t qword;
    uint16_t count;
    uint32_t lanes;
};

// A model's packet into the batch being filled: `qwords` x 16 bytes at `source` (game memory: GT4's model files
// hold their vertex data as these packets, compact formats a decoder expands into the batch), the lane mask its
// masked data uses (`mask`, two bits per lane and cycle: 0 data, 1 the row, 2 the column, 3 keep).
struct BatchModel {
    uint32_t source;
    uint32_t qwords;
    uint32_t mask;
};

// The batch just filled is drawn by the renderer at `entry` (its address in 8-byte units, GT4's program layout),
// with the lighting chain `itop` (0xFFFFFFFF: the previous run's); the next batch fills the other buffer.
struct BatchRun {
    uint32_t entry;
    uint32_t itop;
};

// A texture set the game bound for drawing (00499728 gave it a slot and uploaded it): `id` names it in the drawing
// state (Texture::set) from here on, until a set record with the same id. Its `uploads` TexSetUpload and `records`
// TexSetTexture follow. The runtime's frame walker fills in `hash` (the content of the uploads' bytes when the frame
// is sent) and `sent`: 1 when the bytes follow as GT4_REC_GFX_DATA (each upload's, in order), 0 when the host has
// this content already.
struct TexSetRecord {
    uint32_t id;
    uint32_t hashLow, hashHigh;
    uint16_t uploads, records;
    uint32_t sent;
};
static_assert(sizeof(TexSetRecord) == 20);
// One of its uploads: as an UploadRecord without the rectangle's corner (always 0, 0).
struct TexSetUpload {
    uint32_t destination;   // base (blocks) | width (64-pixel units) << 16 | format << 24, at the set's slot
    uint16_t w, h;
    uint32_t source;        // the pixels in game memory
    uint32_t qwords;
};
static_assert(sizeof(TexSetUpload) == 16);
// One of its textures: the GS register values GT4 keeps for it (TEX0, TEX1, MIPTBP1, MIPTBP2, CLAMP), at the set's slot.
struct TexSetTexture {
    uint64_t tex0, tex1, mip1, mip2, clamp;
};
static_assert(sizeof(TexSetTexture) == 40);

// A vertex: position in 1/16 pixel in the drawing context's coordinates (its origin, at 2048 pixels on
// GT4's screens, included), depth, colour (R in the low byte, alpha 0x80 = 1), texture coordinates as S, T,
// Q (perspective) or U, V (texels in 1/16, with kTexelCoords), fog (255: none).
struct Vertex {
    uint16_t x, y;
    uint32_t z;
    uint32_t rgba;
    float s, t, q;
    uint16_t u, v;
    uint8_t fog;
    uint8_t kick;   // 0: the vertex starts or continues a strip without drawing (the first ones of a strip)
    uint16_t pad;
};
static_assert(sizeof(Vertex) == 32);

// Primitives: `count` Vertex follow. With kDrawPrim the primitive attributes are set to `prim` first (a new
// strip); without it the vertices continue the current primitive. With kDrawFog Z has 24 bits and the
// vertices' fog applies; without it Z has 32 bits and the fog of the last vertex that had one stays. The
// colour (with Q), S, T and Q, and U and V come from the vertices when kDrawColor, kDrawSt and kDrawUv say so;
// otherwise the values the previous vertices left apply.
enum DrawFlags : uint32_t { kDrawPrim = 1, kDrawFog = 2, kDrawColor = 4, kDrawSt = 8, kDrawUv = 16,
                            kDrawAttributesOnly = 32 /* the vertices' attributes are set, nothing is drawn */ };
struct DrawRecord {
    uint16_t prim;    // PrimFlags
    uint16_t count;
    uint32_t flags;   // DrawFlags
};

// The display: what the screen shows from the next VBlank on (the PS2 kept it in its display registers; the
// link record GT4_REC_DISPLAY carries it). Two read circuits, each a rectangle of a frame buffer placed on the
// screen; with both on, circuit 1 is blended over circuit 2 (GT4 uses circuit 2, and circuit 1 as well to blend
// two fields).
struct DisplayCircuit {
    uint8_t enabled = 0;
    uint8_t format = C32;
    uint8_t width = 0;            // the buffer's width, 64-pixel units
    uint8_t magH = 1, magV = 1;   // video clocks per pixel, lines per pixel
    uint8_t pad = 0;
    uint16_t base = 0;            // the buffer, in blocks
    uint16_t x = 0, y = 0;        // the shown rectangle's corner in the buffer, pixels
    uint16_t clocks = 0, lines = 0;   // its size on the screen: video clocks (magH per pixel) and lines
    uint16_t left = 0, top = 0;   // its place on the screen, video clocks and lines
    bool operator==(const DisplayCircuit&) const = default;
};
struct DisplayRecord {
    DisplayCircuit circuit[2];
    uint8_t interlaced = 1;
    uint8_t fieldMode = 0;        // interlaced: 1 each field shows every line of the buffer, 0 every other line
    uint8_t blendFixed = 0;       // circuit 1 over 2 with `alpha` (else with circuit 1's pixel alpha)
    uint8_t alpha = 0;
    uint8_t alphaFromCircuit2 = 0, blendBackground = 0;   // where the output alpha comes from; blend with the background
    uint16_t pad = 0;
    uint32_t background = 0;      // RGB
    bool operator==(const DisplayRecord&) const = default;

    // The pixels of the shown rectangle (GT4's buffers: 640 x 448, or a field's 224 lines in field mode), of
    // circuit 2 when it is on.
    const DisplayCircuit& shown() const { return circuit[1].enabled || !circuit[0].enabled ? circuit[1] : circuit[0]; }
    uint32_t shownWidth() const { const DisplayCircuit& c = shown(); return c.magH ? c.clocks / c.magH : 0; }
    uint32_t shownHeight() const { const DisplayCircuit& c = shown(); return interlaced && fieldMode ? c.lines / 2 : c.lines; }
};
static_assert(sizeof(DisplayRecord) == 52);

// The display from the graphics library's encoding of it (its copy of the display registers PMODE, SMODE2,
// DISPFB1, DISPLAY1, DISPFB2, DISPLAY2 and BGCOLOR, the GS's bit fields).
inline DisplayRecord displayFromRegisters(uint64_t pmode, uint64_t smode2, uint64_t dispfb1, uint64_t display1, uint64_t dispfb2,
                                          uint64_t display2, uint64_t bgcolor) {
    DisplayRecord d;
    const uint64_t fb[2] = {dispfb1, dispfb2}, disp[2] = {display1, display2};
    for (int k = 0; k < 2; ++k) {
        DisplayCircuit& c = d.circuit[k];
        c.enabled = (pmode >> k) & 1;
        c.base = static_cast<uint16_t>((fb[k] & 0x1FF) * 32);
        c.width = static_cast<uint8_t>((fb[k] >> 9) & 0x3F);
        c.format = static_cast<uint8_t>((fb[k] >> 15) & 0x1F);
        c.x = static_cast<uint16_t>((fb[k] >> 32) & 0x7FF);
        c.y = static_cast<uint16_t>((fb[k] >> 43) & 0x7FF);
        c.left = static_cast<uint16_t>(disp[k] & 0xFFF);
        c.top = static_cast<uint16_t>((disp[k] >> 12) & 0x7FF);
        c.magH = static_cast<uint8_t>(((disp[k] >> 23) & 0xF) + 1);
        c.magV = static_cast<uint8_t>(((disp[k] >> 27) & 0x3) + 1);
        c.clocks = static_cast<uint16_t>(((disp[k] >> 32) & 0xFFF) + 1);
        c.lines = static_cast<uint16_t>(((disp[k] >> 44) & 0x7FF) + 1);
    }
    d.alphaFromCircuit2 = (pmode >> 6) & 1;
    d.blendFixed = (pmode >> 5) & 1;
    d.blendBackground = (pmode >> 7) & 1;
    d.alpha = static_cast<uint8_t>((pmode >> 8) & 0xFF);
    d.interlaced = smode2 & 1;
    d.fieldMode = (smode2 >> 1) & 1;
    d.background = static_cast<uint32_t>(bgcolor & 0xFFFFFF);
    return d;
}

constexpr uint32_t recordQwords(uint32_t payload) { return (static_cast<uint32_t>(sizeof(Header)) + payload + 15) / 16; }

}  // namespace gt4::gfx
