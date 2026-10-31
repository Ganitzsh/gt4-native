#pragma once
// The native renderer's front end: the game's GIF packets (all three paths) and privileged register writes
// in, one frame list per VSYNC out. It keeps the GS register state of both drawing contexts, assembles
// primitives (strips, fans, sprites, ADC), batches draws that share their state, applies image transfers to
// the memory model (gs_memory.h), and decides for each textured draw where its texels come from: a render
// target drawn earlier (the pages it last wrote) or GS memory, decoded once per content. The back end
// (Vulkan) only executes the list. See docs/NATIVE_RENDERER.md.
#include "gs_memory.h"
#include "texture_pack.h"
#include "gt4/gfx.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
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

// What a draw's result depends on: its primitive attributes, the state of the context it uses (include/gt4/
// gfx.h; fields that do not matter for it are cleared, so that equal draws batch) and the shared state.
struct DrawState {
    uint32_t prim = 0;   // gfx::PrimFlags: type, Gouraud, textured, fog, blend, antialiased, texel coordinates, context 2
    gfx::Context c;      // the origin cleared (the vertices are in window pixels); the texture and sampler when untextured,
                         // the blend when unblended, the mipmap levels when it does not mipmap
    uint8_t texAlpha0 = 0, texAlpha1 = 0, texBlackTransparent = 0;   // when textured
    uint8_t perPixelBlend = 0, dither = 0, colorClamp = 1;
    uint32_t fogColor = 0;   // when fogged
    bool operator==(const DrawState&) const = default;
};

// The palette fields of a texture (format, palette base, format, storage, offset): what a loaded palette
// is kept against.
inline bool samePalette(const gfx::Texture& a, const gfx::Texture& b) {
    return a.format == b.format && a.clutBase == b.clutBase && a.clutFormat == b.clutFormat && a.clutStorage == b.clutStorage &&
           a.clutOffset == b.clutOffset;
}

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
    // Memory, a texture pack's image: the rectangle of the GS texture it stands for, in GS texels (an axis
    // without one: 0, 0, the whole axis).
    uint16_t regionX = 0, regionY = 0, regionWidth = 0, regionHeight = 0;
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
    bool replaced = false;           // a texture pack's image (any size; the GS texture's shape): sampled normalised
    std::vector<uint32_t> pixels;    // level 0, then each further level
};

// A texture read from a render target through another pixel format (the PS2's channel-shuffle and palette
// tricks: a 32-bit frame read as 8-bit indices through a CLUT): the back end builds it on the GPU, from the
// target as it is at that point of the frame, with the CLUT buffer as it was when the draw was made.
// target = 0: the indices are in GS memory (`indices`, one byte per texel) and only the palette is a target's.
struct Reinterpretation {
    uint32_t target = 0, targetBp = 0, targetBw = 0, targetPsm = 0;
    std::vector<uint8_t> indices;
    gfx::Texture texture;
    uint8_t texAlpha0 = 0, texAlpha1 = 0, texBlackTransparent = 0;
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
    gfx::DisplayRecord record;
    uint32_t target1 = 0, target2 = 0;   // the targets read circuits 1 and 2 show (0: memory, not drawn this way)
};

// One frame: commands in the game's order.
struct FrameList {
    struct Command {
        enum class Kind : uint8_t { Draw, Clear, Light } kind = Kind::Draw;
        uint32_t index = 0;   // into draws (Light: into lights)
    };
    // A 3D scene complete in its target (the next draw reads it into another): the back end may relight it
    // there with its Z buffer (docs/LIGHTING.md).
    struct Lighting {
        uint32_t target = 0, depth = 0;
        uint32_t width = 0, height = 0;   // the scene's area from the target's corner (its 3D draws' scissor), GS pixels
    };
    std::vector<Lighting> lights;
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
    bool displayed = false;   // ends at a VSYNC (display set); a list cut for a GS -> host read does not
    bool dump = false;   // debugging: the renderer writes this frame's targets out (GT4_GSN_TRIGGER)
    void clear();
};

// A GS -> host read of pixels a draw made: the rectangle of a colour target in GS pixels, wanted packed in
// `psm` (the target's own format and layout), from the GPU.
struct ReadRequest {
    uint32_t target = 0, x = 0, y = 0, width = 0, height = 0, psm = 0;
};

// A frame that will not be drawn: its texture uploads and retirements move to the next one, `into`.
void carryOver(FrameList& dropped, FrameList& into);

class GsFrontend {
public:
    // SIGNAL, FINISH and LABEL as packets of any path write them: kind 0 FINISH, 1 SIGNAL, 2 LABEL, with the
    // register's value (include/gt4/link/protocol.h GT4_REC_GS_EVENT; the game applies CSR, IMR and SIGLBLID).
    using EventSink = std::function<void(uint32_t kind, uint64_t value)>;

    GsFrontend();
    void gif(uint32_t path, const uint8_t* data, size_t bytes);
    // Primitives from GT4's 3D renderers (platform/gs/renderers3d.h), with no GIF packet in between: PRIM when
    // the batch's tag sets it, then per vertex the values the ST, RGBAQ and XYZF2 / XYZ2 registers receive
    // (XYZF3 / XYZ3 when `adc`: the vertex completes no triangle).
    struct InputVertex {
        float s = 0, t = 0, q = 1;
        uint32_t rgba = 0;
        uint32_t x = 0, y = 0, z = 0, fog = 0;   // x, y in 12.4; z 24 bits with fog, 32 without
        bool adc = false;
    };
    void setPrim(uint32_t prim);
    void vertex(const InputVertex& v, bool withFog);
    // The display from this VBlank on.
    void display(const gfx::DisplayRecord& d) { shown = d; }
    // Ends the frame: `out` receives it (swapped, so its buffers are reused).
    void vsync(uint64_t vblank, FrameList& out);
    const GsMemory& memory() const { return mem; }
    void setEventSink(EventSink sink) { events = std::move(sink); }
    // A texture pack's replacements for textures in GS memory (texture_pack.h), used while switched on
    // (setReplacements may be called from any thread; it takes effect at the next frame).
    void setTexturePack(std::shared_ptr<TexturePack> pack) { texturePack = std::move(pack); }
    void setReplacements(bool on) { replacementsWanted.store(on); }
    // GS -> host data of the transfer the last TRXDIR = 1 started, `qwc` qwords from where the previous call
    // stopped (zeros past its end). Read from the memory model; `readFromTarget` says whether a page of it
    // was last written by a draw (whose pixels only the GPU has: those bytes are stale here).
    void readTransfer(void* out, uint32_t qwc);
    bool readFromTarget() const { return readTarget; }
    // When the read in progress is of one colour target in the target's own layout: the rectangle to read
    // on the GPU (once; then cut the frame list there and give the pixels to setReadData).
    bool takeGpuRead(ReadRequest& request);
    void setReadData(std::vector<uint8_t> data);
    // Ends the frame list at this point without a VSYNC (`out` receives it): what the GPU must draw before
    // a read. The frame goes on in a new list.
    void cut(FrameList& out);
    // Replaces the whole of GS memory (4 MiB in address order: a saved state restored); every page becomes
    // memory-owned and changed.
    void restoreMemory(const void* data);
    // Forgets every decoded texture (after frames that were not drawn: their uploads never happened); the
    // next frame says so with resetTextures.
    void resetTextures();

    struct Stats {
        uint64_t primitives = 0, draws = 0, uploads = 0, copies = 0, decoded = 0, replaced = 0, targetReads = 0, reinterpreted = 0;
        uint64_t named = 0, found = 0;   // textures looked up in the texture pack, and those it has
        uint64_t droppedDraws = 0;   // past the target limit (targetFor)
        uint64_t targetsReused = 0;  // slots given to a new buffer at the limit
        uint64_t reads = 0, readsFromTargets = 0;   // LOCAL -> HOST transfers; those with drawn pages
    } stats;

    // The state groups of include/gt4/gfx.h: a state record from the game (the shared groups, then the
    // context groups of both contexts in bit order, from one source context).
    void applyState(const gfx::StateRecord& r);
    // An upload record: `bytes` of packed pixels into graphics memory.
    void upload(const gfx::UploadRecord& u, const uint8_t* data, size_t bytes);
    // A draw record: primitives.
    void draw(const gfx::DrawRecord& d, const gfx::Vertex* vertices);
    // A read record: the pixels for readTransfer.
    void read(const gfx::ReadRecord& r);

private:
    GsMemory mem;
    FrameList frame;
    // The drawing state, natively (state records and the GS registers of GIF packets both set it), and the
    // registers of GIF packets that are not drawing state (vertices, transfers).
    gfx::Context contexts[2];
    gfx::Shared shared;
    std::array<uint64_t, 128> reg{};
    gfx::DisplayRecord shown;
    void contextGroupSet(uint32_t context, uint32_t group);   // a group's side effects (palette loads)

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
    // The LOCAL -> HOST transfer in progress: its packed pixels, how far they have been read.
    std::vector<uint8_t> readData;
    size_t readPos = 0;
    bool readTarget = false;
    ReadRequest gpuRead;
    bool gpuReadPending = false;
    EventSink events;

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
    // The target the CLUT buffer was last loaded from (0: memory), and the texture of that load (samePalette).
    struct ClutSource {
        uint32_t target = 0;
        gfx::Texture texture;
    } clutSource;
    void clutWritten(const gfx::Texture& texture);

    std::shared_ptr<TexturePack> texturePack;
    std::atomic<bool> replacementsWanted{true};
    bool replacementsOn = false;
    // Decoded textures, by what they were decoded from.
    struct Cached {
        uint32_t id = 0, version = 0;
        uint64_t generation = 0;   // sum of the covered pages' generations when decoded
        uint64_t clutHash = 0;
        uint64_t decodedFrame = ~0ull, lastUsed = 0;
        uint64_t texels = 0;
        bool replacementFound = false;     // the texture pack has an image for this content
        uint32_t replacedId = 0;           // ... the texture drawn for it (replacementTexture), 0: the decoded one
        TextureName replacementName;
        uint32_t replacementOrigin[2] = {};   // the GS texel its region starts at (the region's size is the name's)
        bool replacementPending = false;   // ... still loading: looked up again after more loads
        uint64_t packLoads = 0;            // ... the pack's loadedSerial when it was looked up
    };
    uint64_t cachedTexels = 0;   // of every cached texture
    struct ReplacementTexture {
        uint32_t id = 0;
        uint64_t lastUsed = 0, texels = 0;
    };
    std::unordered_map<TextureName, ReplacementTexture, TextureNameHash> replacementTextures;
    uint32_t replacementTexture(const TextureName& name, const ReplacementImage& image);
    std::unordered_map<uint64_t, Cached> textureCache;
    // Per target this frame: its depth-tested 3D draws since it was last lit, and their Z buffer (lighting).
    struct Scene {
        uint32_t draws = 0, depth = 0, width = 0, height = 0;
    };
    std::unordered_map<uint32_t, Scene> scenes;
    void sceneRead(uint32_t target);
    uint32_t nextTexture = 1;
    uint64_t frameCount = 0;

    void write(uint32_t r, uint64_t value);
    void kick(bool draw);
    void emit(const Vertex* v, uint32_t n, Topology topology);
    DrawState currentState(uint32_t prim) const;
    uint32_t primValue() const;
    uint32_t targetFor(uint32_t bp, uint32_t psm, bool depth, uint32_t heightNeeded, uint32_t bw);
    void markWritten(uint32_t targetId, uint32_t bp, uint32_t bw, uint32_t psm, uint32_t x0, uint32_t x1, uint32_t y0, uint32_t y1);
    TextureSource textureFor(const DrawState& s);
    void transferStarted(uint32_t dir);
    void readStarted(const GsMemory::Transfer& t);
    // GS memory written by a transfer (changed: its pixels are known to differ; uploads say so as their
    // data arrives, imageWritten): the pages' owners and generations.
    void memoryWritten(uint32_t bp, uint32_t bw, uint32_t psm, uint32_t x, uint32_t y, uint32_t w, uint32_t h, bool changed = true);
    void imageWritten(const uint8_t* bytes, size_t size);
    std::vector<uint32_t> uploadPages;   // the pages of the upload in progress
};

}  // namespace gt4::gsn
