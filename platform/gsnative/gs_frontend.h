#pragma once
// The native renderer's front end: the game's GIF packets (all three paths) and privileged register writes
// in, one frame list per VSYNC out. It keeps the GS register state of both drawing contexts, assembles
// primitives (strips, fans, sprites, ADC), batches draws that share their state, applies image transfers to
// the memory model (gs_memory.h), and decides for each textured draw where its texels come from: a render
// target drawn earlier (the pages it last wrote) or GS memory, decoded once per content. The back end
// (Vulkan) only executes the list. See docs/NATIVE_RENDERER.md.
#include "gs_memory.h"

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace gt4::gsn {

struct Vertex {
    float x = 0, y = 0;      // window coordinates in pixels (XYZ minus XYOFFSET, over 16)
    uint32_t z = 0;          // as written: 32 bits, or 24 for XYZF and the 24-bit Z formats
    uint32_t rgba = 0;       // R in the low byte; A as the GS keeps it (0x80 = 1.0)
    float s = 0, t = 0, q = 1;   // STQ (FST = 0)
    float u = 0, v = 0;      // UV in texels (FST = 1)
    uint32_t fog = 0;        // F, 0..255
    // A sprite's texture rectangle (UV in texels, or S/Q and T/Q): filtering at a higher resolution stays
    // inside it. minU > maxU for other primitives.
    float minU = 1, minV = 1, maxU = 0, maxV = 0;
};

// The registers a draw's result depends on, for the context it uses.
struct DrawState {
    uint32_t prim = 0;   // TYPE (low 3 bits), IIP, TME, FGE, ABE, AA1, FST, CTXT, FIX as in PRIM
    uint64_t frame = 0, zbuf = 0, tex0 = 0, tex1 = 0, clamp = 0, test = 0, alpha = 0, scissor = 0, texa = 0;
    uint64_t miptbp1 = 0, miptbp2 = 0;   // mipmap levels 1-3 and 4-6, when TEX1 mipmaps
    uint32_t fba = 0, pabe = 0, dthe = 0, colclamp = 1, fogcol = 0;
    bool operator==(const DrawState&) const = default;
};

enum class Topology : uint8_t { Points, Lines, Triangles };

// Where a draw's texels come from.
struct TextureSource {
    enum class Kind : uint8_t { None, Memory, Target, Reinterpreted } kind = Kind::None;
    uint32_t id = 0;        // Memory: a texture of FrameList::textures (by id); Target: a target id;
                            // Reinterpreted: an index into FrameList::reinterpretations
    uint32_t version = 0;   // Memory: the content version
    // Target: the texture's texel (0, 0) in the target's pixels, and whether the target's format can be
    // sampled as the texture's (same layout); otherwise the texels are decoded from memory, which is stale.
    int32_t offsetX = 0, offsetY = 0;
    bool reinterpreted = false;
};

struct Draw {
    DrawState state;
    Topology topology = Topology::Triangles;
    uint32_t first = 0, count = 0;   // vertices
    uint32_t target = 0, depth = 0;  // target ids (depth 0: none)
    TextureSource texture;
};

// A decoded texture the back end must (re)upload: RGBA8, R in the low byte, the GS's alpha (0x80 = 1.0).
struct TextureImage {
    uint32_t id = 0, version = 0, width = 0, height = 0;
    uint32_t levels = 1;             // mipmap levels, each half the size of the one before (at least 1 x 1)
    std::vector<uint32_t> pixels;    // level 0, then each further level
};

// A texture read from a render target through another pixel format (the PS2's channel-shuffle and palette
// tricks: a 32-bit frame read as 8-bit indices through a CLUT): the back end builds it on the GPU, from the
// target as it is at that point of the frame, with the CLUT buffer as it was when the draw was made.
// target = 0: the indices are in GS memory (`indices`, one byte per texel) and only the palette is a target's.
struct Reinterpretation {
    uint32_t target = 0, targetBp = 0, targetBw = 0, targetPsm = 0;
    std::vector<uint8_t> indices;
    uint64_t tex0 = 0, texa = 0;
    uint32_t width = 0, height = 0;
    std::array<uint8_t, 1024> clut{};
    // When the CLUT was loaded from a 32-bit target (GT4 renders palettes and then uses them): that target,
    // read on the GPU instead of `clut`.
    uint32_t clutTarget = 0, clutTargetBp = 0, clutTargetBw = 0;
};

// A render target: a frame or Z buffer at FBP (blocks) / FBW (64-pixel units) / PSM, as tall as drawn.
struct Target {
    uint32_t id = 0, bp = 0, bw = 0, psm = 0, height = 0;
    bool depth = false;
    uint32_t generation = 0;   // a new buffer in this slot (the id is reused): the back end starts a new image
    uint64_t lastUsed = 0;     // the front end's frame count when last drawn into, sampled or displayed
};

struct Display {
    uint64_t pmode = 0, smode2 = 0, dispfb1 = 0, display1 = 0, dispfb2 = 0, display2 = 0, bgcolor = 0;
    uint32_t target1 = 0, target2 = 0;   // the targets DISPFB1/2 point at (0: memory, not drawn this way)
};

// One frame: commands in the game's order.
struct FrameList {
    struct Command {
        enum class Kind : uint8_t { Draw, Clear } kind = Kind::Draw;
        uint32_t index = 0;   // into draws
    };
    std::vector<Vertex> vertices;
    std::vector<Draw> draws;
    std::vector<Command> commands;
    std::vector<TextureImage> textures;   // new or changed this frame
    std::vector<Reinterpretation> reinterpretations;
    std::vector<uint32_t> retiredTextures;   // texture ids no longer used after this frame
    bool resetTextures = false;              // every earlier texture is gone (GsFrontend::resetTextures)
    std::vector<Target> targets;          // every target known (created or grown this frame included)
    Display display;
    uint64_t vblank = 0;
    bool dump = false;   // debugging: the renderer writes this frame's targets out (GT4_GSN_TRIGGER)
    void clear();
};

// A frame that will not be drawn: its texture uploads and retirements move to the next one, `into`.
void carryOver(FrameList& dropped, FrameList& into);

class GsFrontend {
public:
    GsFrontend();
    void gif(uint32_t path, const uint8_t* data, size_t bytes);
    void privileged(uint32_t offset, uint64_t value);
    // Ends the frame: `out` receives it (swapped, so its buffers are reused).
    void vsync(uint64_t vblank, FrameList& out);
    const GsMemory& memory() const { return mem; }
    // Forgets every decoded texture (after frames that were not drawn: their uploads never happened); the
    // next frame says so with resetTextures.
    void resetTextures();

    struct Stats {
        uint64_t primitives = 0, draws = 0, uploads = 0, copies = 0, decoded = 0, targetReads = 0, reinterpreted = 0;
        uint64_t droppedDraws = 0;   // past the target limit (targetFor)
        uint64_t targetsReused = 0;  // slots given to a new buffer at the limit
    } stats;

private:
    GsMemory mem;
    FrameList frame;
    std::array<uint64_t, 128> reg{};
    uint64_t priv[0x100]{};

    // Vertex queue of the current primitive.
    std::array<Vertex, 3> queue;
    uint32_t queued = 0, stripParity = 0;
    float st[2] = {}, q = 1;   // ST and Q as set by ST/RGBAQ
    // Each GIF path's tag in progress: its data may continue in the next transfer (an IMAGE tag in one DMA
    // packet, its pixels in the next).
    struct GifPath {
        uint64_t regs = 0;
        uint32_t loops = 0;    // loops left (IMAGE: qwords left)
        uint32_t flg = 0, nreg = 0;
        uint32_t reg = 0;      // the register within the loop (PACKED), or the 64-bit word (REGLIST)
    } gifPaths[4];
    void packed(uint32_t r, uint64_t a, uint64_t b);
    uint32_t packetsThisFrame = 0;   // GIF transfers since the last VSync (debugging logs)
    // PACKED/REGLIST state that spans calls is not kept: GT4's packets are whole per call.

    // Image transfer (HOST -> LOCAL) in progress.
    bool imageActive = false;

    // Targets and the owner of each 8 KiB page (0: memory; else a target id), with when it was written.
    std::vector<Target> targets;
    struct PageOwner {
        uint32_t target = 0;
        uint64_t serial = 0;
    };
    std::vector<PageOwner> pages;   // 512 pages
    std::vector<uint64_t> pageGeneration;   // bumped by every transfer into the page
    uint64_t serial = 0;
    std::vector<uint64_t> targetWritten;   // per target id: the serial of its last write
    struct LastReinterpretation {
        uint32_t index = ~0u;
        uint64_t serial = 0, uploads = 0;
    } lastReinterpretation, lastMemoryReinterpretation;
    uint64_t uploads = 0;   // pages written by transfers, ever
    // The target the CLUT buffer was last loaded from (0: memory), and the TEX0 fields of that load.
    static constexpr uint64_t kClutFields = (0x3Full << 20) | (0xFFFFFFull << 37);
    struct ClutSource {
        uint32_t target = 0;
        uint64_t tex0 = 0;
    } clutSource;
    void clutWritten(uint64_t tex0);

    // Decoded textures, by what they were decoded from.
    struct Cached {
        uint32_t id = 0, version = 0;
        uint64_t generation = 0;   // sum of the covered pages' generations when decoded
        uint64_t clutHash = 0;
        uint64_t decodedFrame = ~0ull, lastUsed = 0;
        uint64_t texels = 0;
    };
    uint64_t cachedTexels = 0;   // of every cached texture
    std::unordered_map<uint64_t, Cached> textureCache;
    uint32_t nextTexture = 1;
    uint64_t frameCount = 0;

    void write(uint32_t r, uint64_t value);
    void kick(bool draw);
    void emit(const Vertex* v, uint32_t n, Topology topology);
    DrawState currentState(uint32_t prim) const;
    uint32_t primValue() const;
    uint32_t targetFor(uint64_t frameReg, bool depth, uint32_t heightNeeded, uint32_t bw);
    void markWritten(uint32_t targetId, uint32_t bp, uint32_t bw, uint32_t psm, uint32_t x0, uint32_t x1, uint32_t y0, uint32_t y1);
    TextureSource textureFor(const DrawState& s);
    void transferStarted(uint32_t dir);
    // GS memory written by a transfer (changed: its pixels are known to differ; uploads say so as their
    // data arrives, imageWritten): the pages' owners and generations.
    void memoryWritten(uint32_t bp, uint32_t bw, uint32_t psm, uint32_t x, uint32_t y, uint32_t w, uint32_t h, bool changed = true);
    void imageWritten(const uint8_t* bytes, size_t size);
    std::vector<uint32_t> uploadPages;   // the pages of the upload in progress
};

}  // namespace gt4::gsn
