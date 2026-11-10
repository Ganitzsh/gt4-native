// The native renderer's front end (see gs_frontend.h). Register layouts from the public GS documentation.
#include <map>   // R10 spike (local)
#include <array>     // LOCAL N3 DEBUG
#include <numeric>   // LOCAL N3 DEBUG
#include <unordered_set>   // LOCAL N3 DEBUG
static std::unordered_set<uint64_t> n3SkipIdentities;   // LOCAL N3 DEBUG: identities judged v4 (GT4_N3_V4SKIP)
#include "gs_frontend.h"

#include <chrono>
#include "../gs/cpu_profile.h"
#include "texture_name.h"

#include <algorithm>
#include <cmath>
#include <bit>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

namespace gt4::gsn {

namespace {

enum Reg : uint32_t {
    PRIM = 0x00, RGBAQ = 0x01, ST = 0x02, UV = 0x03, XYZF2 = 0x04, XYZ2 = 0x05, TEX0_1 = 0x06, TEX0_2 = 0x07,
    CLAMP_1 = 0x08, CLAMP_2 = 0x09, FOG = 0x0A, XYZF3 = 0x0C, XYZ3 = 0x0D, AD = 0x0E, NOP = 0x0F,
    TEX1_1 = 0x14, TEX1_2 = 0x15, MIPTBP1_1 = 0x34, MIPTBP1_2 = 0x35, MIPTBP2_1 = 0x36, MIPTBP2_2 = 0x37, TEX2_1 = 0x16, TEX2_2 = 0x17, XYOFFSET_1 = 0x18, XYOFFSET_2 = 0x19,
    PRMODECONT = 0x1A, PRMODE = 0x1B, TEXCLUT = 0x1C, TEXA = 0x3B, FOGCOL = 0x3D, TEXFLUSH = 0x3F,
    SCISSOR_1 = 0x40, ALPHA_1 = 0x42, DTHE = 0x45, COLCLAMP = 0x46, TEST_1 = 0x47, PABE = 0x49, FBA_1 = 0x4A,
    FRAME_1 = 0x4C, ZBUF_1 = 0x4E, BITBLTBUF = 0x50, TRXPOS = 0x51, TRXREG = 0x52, TRXDIR = 0x53, HWREG = 0x54,
    SIGNAL = 0x60, FINISH = 0x61, LABEL = 0x62,
};

float asFloat(uint32_t bits) { return std::bit_cast<float>(bits); }

// The mask on XYOFFSET's fraction for the draws. The register as the GS has it (R6: closer to PCSX2's software
// renderer than half-pixel steps in the race, its menus and the garage, and no field shake at 1x to 4x on today's
// tree; see CAPTURES' re-baselines). GT4_GSN_JITTER=mask keeps half-pixel steps, original's picture before R6
// (d3a50ff), for comparisons.
uint32_t originMask() {
    static const uint32_t mask = [] {
        const char* e = std::getenv("GT4_GSN_JITTER");
        return e && !std::strcmp(e, "mask") ? 0xFFF8u : 0xFFFFu;
    }();
    return mask;
}

uint64_t mix(uint64_t h, uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    return h * 0xFF51AFD7ED558CCDull;
}

// Page dimensions of a format (pixels).
void pageSize(uint32_t psm, uint32_t& w, uint32_t& h) {
    switch (psm) {
    case PSMCT16: case PSMCT16S: case PSMZ16: case PSMZ16S: w = 64; h = 64; break;
    case PSMT8: w = 128; h = 64; break;
    case PSMT4: w = 128; h = 128; break;
    default: w = 64; h = 32; break;   // 32-bit layouts, including T8H/T4HL/T4HH
    }
}

bool layout32(uint32_t psm) {
    return psm == PSMCT32 || psm == PSMCT24 || psm == PSMT8H || psm == PSMT4HL || psm == PSMT4HH;
}

// A narrow 16-bit colour target: GT4's car pictures, which it reads back (Target::exact).
bool readBackShape(bool depth, uint32_t psm, uint32_t bw) {
    return !depth && bw * 64 < 512 && (psm == PSMCT16 || psm == PSMCT16S);
}

}  // namespace

void FrameList::clear() {
    vertices.clear();
    draws.clear();
    commands.clear();
    lights.clear();
    textures.clear();
    reinterpretations.clear();
    models.clear();
    modelData.clear();
    effects.clear();
    modelVertices = 0;
    retiredTextures.clear();
    resetTextures = false;
    targets.clear();
    displayed = false;
    dump = false;
    readbacks.clear();
    scene.reset();
    sceneCache.reset();
    for (ViewOnList& v : views) {
        scene::sceneViewToPool(v.package);
        if (v.ids.capacity()) {   // (B6: kept for this list's next views)
            v.ids.clear();
            spareIds.push_back(std::move(v.ids));
        }
        if (v.materials.capacity()) {
            v.materials.clear();
            spareMaterials.push_back(std::move(v.materials));
        }
        if (v.items.capacity()) {
            v.items.clear();
            spareItems.push_back(std::move(v.items));
        }
    }
    views.clear();
}

namespace {
std::mutex spareMutex;
std::vector<FrameList> spareLists;
}  // namespace

void recycleFrameList(FrameList&& list) {
    list.clear();
    list.display = Display{};
    list.vblank = 0;
    std::lock_guard<std::mutex> lock(spareMutex);
    if (spareLists.size() < 4) spareLists.push_back(std::move(list));
}

FrameList spareFrameList() {
    std::lock_guard<std::mutex> lock(spareMutex);
    if (spareLists.empty()) return FrameList{};
    FrameList list = std::move(spareLists.back());
    spareLists.pop_back();
    return list;
}

namespace {
std::atomic<uint64_t> skippedWithScene{0};   // lists the renderer skipped with a scene frame or view packages (a.8)
}  // namespace

uint64_t listsSkippedWithScene() { return skippedWithScene.load(); }

void carryOver(FrameList& dropped, FrameList& into) {
    into.dump = into.dump || dropped.dump;
    // Its asynchronous reads are never dropped (the game would wait for them): copied after the next list, the target
    // as that one leaves it.
    if (!dropped.readbacks.empty()) {
        static std::atomic<uint64_t> carried{0};
        const uint64_t total = carried += dropped.readbacks.size();
        if (total <= 10 || total % 100 == 0)
            std::printf("gsn: %zu asynchronous read(s) of a skipped list carried over to the next (%llu so far)\n",
                        dropped.readbacks.size(), static_cast<unsigned long long>(total));
        into.readbacks.insert(into.readbacks.begin(), dropped.readbacks.begin(), dropped.readbacks.end());
        dropped.readbacks.clear();
    }
    // The next list starts with every earlier texture gone (GsFrontend::resetTextures): the dropped list's uploads and
    // retirements came before that reset, which the front end has applied, so they go with the list. Moved in, they would
    // be uploaded after the reset and never retired (the front end forgot those ids). A reset in the dropped list alone
    // keeps the order below: the reset, its uploads, then the next list's.
    if (into.resetTextures) {
        dropped.textures.clear();
        dropped.retiredTextures.clear();
    }
    if (!dropped.textures.empty()) {
        for (auto& t : into.textures) dropped.textures.push_back(std::move(t));   // the later version wins
        into.textures = std::move(dropped.textures);
    }
    into.retiredTextures.insert(into.retiredTextures.end(), dropped.retiredTextures.begin(), dropped.retiredTextures.end());
    into.resetTextures = into.resetTextures || dropped.resetTextures;
    dropped.textures.clear();
    dropped.retiredTextures.clear();
    // Its scene frame and views' packages go with it, never drawn (the next list's draws are not theirs): counted when
    // it had views (a vehicles-only frame, a.1 mode 2, has none: nothing a substitution would have drawn).
    const bool withViews = !dropped.views.empty() || (dropped.scene && !dropped.scene->views.empty());
    if (dropped.scene || !dropped.views.empty()) {
        const uint64_t total = withViews ? ++skippedWithScene : skippedWithScene.load();
        if (withViews && (total <= 10 || total % 100 == 0))
            std::printf("gsn: a skipped list's scene frame or view packages dropped with it (%llu so far)\n",
                        static_cast<unsigned long long>(total));
        dropped.scene.reset();
        for (FrameList::ViewOnList& v : dropped.views) scene::sceneViewToPool(v.package);
        dropped.views.clear();
    }
}

GsFrontend::GsFrontend() : pages(512), pageGeneration(512, 0) {
    if (const char* e = std::getenv("GT4_NATIVE_TEXTURES")) nativeTextures = *e != '0';
    if (const char* e = std::getenv("GT4_NATIVE_EFFECTS")) nativeEffects = *e != '0';
}

uint32_t GsFrontend::primValue() const {
    // Without attributesFromPrim (PRMODECONT = 0) the attributes (everything but the type) come from PRMODE.
    const uint64_t p = reg[PRIM];
    if (shared.attributesFromPrim) return static_cast<uint32_t>(p & 0x7FF);
    return static_cast<uint32_t>((p & 7) | (reg[PRMODE] & 0x7F8));
}

DrawState GsFrontend::currentState(uint32_t prim) const {
    DrawState s;
    s.prim = prim;
    s.c = contexts[(prim & gfx::kContext2) ? 1 : 0];
    s.c.originX = s.c.originY = 0;
    s.texAlpha0 = shared.texAlpha0;
    s.texAlpha1 = shared.texAlpha1;
    s.texBlackTransparent = shared.texBlackTransparent;
    s.perPixelBlend = shared.perPixelBlend;
    s.dither = shared.dither;
    s.colorClamp = shared.colorClamp;
    s.fogColor = shared.fogColor;
    // Mipmapping: levels beyond the first with a minification filter that uses them.
    gfx::Sampler& sm = s.c.sampler;
    if (!(sm.mipMax && sm.minFilter >= 2)) {
        for (uint32_t k = 0; k < 6; ++k) sm.mipBase[k] = sm.mipWidth[k] = 0;
    }
    // Fields that do not matter for the draw are cleared, so that equal draws batch.
    if (!(prim & gfx::kTextured)) {
        s.c.texture = {};
        s.c.sampler = {};
        s.texAlpha0 = s.texAlpha1 = s.texBlackTransparent = 0;
    }
    if (!(prim & gfx::kBlend)) s.c.blendA = s.c.blendB = s.c.blendC = s.c.blendD = s.c.blendFixed = 0;
    if (!(prim & gfx::kFog)) s.fogColor = 0;
    return s;
}

void GsFrontend::contextGroupSet(uint32_t context, uint32_t group) {
    // A texture (or its palette) set: the palette is loaded when the texture says so.
    if (group == gfx::kTexture || group == gfx::kPalette) clutWritten(contexts[context].texture);
}

void GsFrontend::applyState(const gfx::StateRecord& r) {
    if (skipping) return;
    ++stateSerial;
    gfx::applyState(shared, contexts, r, [this](uint32_t context, uint32_t group) { contextGroupSet(context, group); });
}

void GsFrontend::upload(const gfx::UploadRecord& u, const uint8_t* data, size_t bytes) {
    GT4_CPU_SCOPE("front end: uploads");
    GsMemory::Transfer t;
    t.dbp = u.base;
    t.dbw = u.width;
    t.dpsm = u.format;
    t.dsax = u.x;
    t.dsay = u.y;
    t.rrw = u.w;
    t.rrh = u.h;
    if (!setUploadsToo.empty()) {
        auto it = std::find(setUploadsToo.begin(), setUploadsToo.end(), std::make_pair(u.source, uint32_t(u.base)));
        if (it != setUploadsToo.end()) {
            setUploadsToo.erase(it);
            if (nativeTextures) return;
        }
    }
    static const bool logTransfers = std::getenv("GT4_GSN_XFER") != nullptr;
    if (logTransfers)
        std::printf("[gsn upload] dst %X/%u psm %X (%u,%u) %ux%u, %zu bytes from %08X\n", t.dbp, t.dbw, t.dpsm, t.dsax, t.dsay, t.rrw, t.rrh,
                    bytes, u.source);
    mem.beginWrite(t);
    memoryWritten(t.dbp, t.dbw, t.dpsm, t.dsax, t.dsay, t.rrw, t.rrh, false);
    ++stats.uploads;
    if (bytes) imageWritten(data, bytes);
    imageActive = false;
}

void GsFrontend::draw(const gfx::DrawRecord& d, const gfx::Vertex* vertices) {
    GT4_CPU_SCOPE("front end: draw records");
    if (skipping) return;
    if (d.flags & gfx::kDrawPrim) write(PRIM, d.prim & 0x7FF);
    const bool fog = d.flags & gfx::kDrawFog;
    for (uint32_t k = 0; k < d.count; ++k) {
        const gfx::Vertex& v = vertices[k];
        if (d.flags & gfx::kDrawColor) {
            q = v.q;
            reg[RGBAQ] = v.rgba | static_cast<uint64_t>(std::bit_cast<uint32_t>(v.q)) << 32;
        }
        if (d.flags & gfx::kDrawSt) {   // S, T and Q
            st[0] = v.s;
            st[1] = v.t;
            q = v.q;
        }
        if (d.flags & gfx::kDrawUv) reg[UV] = v.u | static_cast<uint64_t>(v.v) << 16;
        if (d.flags & gfx::kDrawAttributesOnly) continue;
        const uint64_t xy = v.x | static_cast<uint64_t>(v.y) << 16;
        if (fog) write(v.kick ? XYZF2 : XYZF3, xy | static_cast<uint64_t>(v.z & 0xFFFFFF) << 32 | static_cast<uint64_t>(v.fog) << 56);
        else write(v.kick ? XYZ2 : XYZ3, xy | static_cast<uint64_t>(v.z) << 32);
    }
}

void GsFrontend::write(uint32_t r, uint64_t value) {
    r &= 0x7F;
    // Drawing state: into the native state (include/gt4/gfx.h).
    bool isShared = false;
    uint32_t context = 0, group = 0;
    if (gfx::groupOfRegister(r, isShared, context, group)) {
        ++stateSerial;
        if (isShared) {
            gfx::setSharedGroup(shared, group, value);
            return;
        }
        if (group == gfx::kOrigin) {   // GT4_GSN_XFER (debugging): offset changes
            static const bool logOffsets = std::getenv("GT4_GSN_XFER") != nullptr;
            const gfx::Context& c = contexts[context];
            if (logOffsets && (c.originX != (value & 0xFFFF) || c.originY != ((value >> 32) & 0xFFFF)))
                std::printf("[gsn offset] %u: %llX\n", context + 1, static_cast<unsigned long long>(value));
        }
        gfx::setContextGroup(contexts[context], group, value);
        contextGroupSet(context, group);
        return;
    }
    switch (r) {
    case PRIM:
        reg[PRIM] = value;
        queued = 0;
        stripParity = 0;
        return;
    case RGBAQ:
        reg[RGBAQ] = value;
        q = asFloat(static_cast<uint32_t>(value >> 32));
        return;
    case ST:
        reg[ST] = value;
        st[0] = asFloat(static_cast<uint32_t>(value));
        st[1] = asFloat(static_cast<uint32_t>(value >> 32));
        return;
    case XYZF2: case XYZ2: case XYZF3: case XYZ3: {
        reg[r] = value;
        Vertex& v = queue[std::min<uint32_t>(queued, 2)];
        const uint32_t prim = primValue();
        const gfx::Context& c = contexts[(prim & gfx::kContext2) ? 1 : 0];
        const uint32_t jitter = originMask();
        const uint32_t x = static_cast<uint32_t>(value & 0xFFFF), y = static_cast<uint32_t>((value >> 16) & 0xFFFF);
        v.x = (static_cast<float>(x) - static_cast<float>(c.originX & jitter)) / 16.0f;
        v.y = (static_cast<float>(y) - static_cast<float>(c.originY & jitter)) / 16.0f;
        const bool f = r == XYZF2 || r == XYZF3;
        v.z = f ? static_cast<uint32_t>((value >> 32) & 0xFFFFFF) : static_cast<uint32_t>(value >> 32);
        v.fog = f ? static_cast<uint32_t>(value >> 56) : static_cast<uint32_t>(reg[FOG] >> 56);
        v.rgba = static_cast<uint32_t>(reg[RGBAQ]);
        v.s = st[0];
        v.t = st[1];
        v.q = q;
        v.u = static_cast<float>(reg[UV] & 0x3FFF) / 16.0f;
        v.v = static_cast<float>((reg[UV] >> 16) & 0x3FFF) / 16.0f;
        // the previous position, less this frame's offset (as x and y): (x - ox) w = x w - ox w
        v.mw = motion[2];
        v.mx = motion[0] - static_cast<float>(c.originX & jitter) / 16.0f * motion[2];
        v.my = motion[1] - static_cast<float>(c.originY & jitter) / 16.0f * motion[2];
        motion[0] = motion[1] = motion[2] = 0.0f;
        v.w = homogeneous[3];
        if (v.w != 0.0f) {
            v.x = homogeneous[0] - static_cast<float>(c.originX & jitter) / 16.0f * v.w;
            v.y = homogeneous[1] - static_cast<float>(c.originY & jitter) / 16.0f * v.w;
            v.z = std::bit_cast<uint32_t>(homogeneous[2]);
            homogeneous[3] = 0.0f;
        }
        kick(r == XYZF2 || r == XYZ2);
        return;
    }
    case TRXDIR:
        reg[r] = value;
        transferStarted(static_cast<uint32_t>(value & 3));
        return;
    case HWREG:
        if (imageActive) imageWritten(reinterpret_cast<const uint8_t*>(&value), 8);
        return;
    case SIGNAL: case FINISH: case LABEL:
        reg[r] = value;
        if (events) events(r == FINISH ? 0 : r == SIGNAL ? 1 : 2, value);
        return;
    default:
        reg[r] = value;
        return;
    }
}

// A texture (or its palette) set. A CLUT loaded from a page a 32-bit target owns holds what was drawn there,
// which only the GPU has: the reinterpretations of draws with this palette read it from that target.
void GsFrontend::clutWritten(const gfx::Texture& texture) {
    const GsMemory::Tex0 t = GsMemory::Tex0::of(texture);
    if (!mem.tex0Written(t)) return;
    ++clutLoads;
    ++stats.clutLoads;
    // A palette in a page that holds a set's content (not in graphics memory): from the content, each (content,
    // place, load) read once.
    if (nativeTextures) {
        const uint32_t page = (t.cbp / 32) & 511;
        const SetPage& sp = setPages[page];
        const bool held = sp.id && !sp.materialized && memorySerial[page] == sp.serial;
        auto content = held ? setContents.find(sp.hash) : setContents.end();
        if (content != setContents.end()) {
            GsMemory::Tex0 own = t;
            own.cbp = (t.cbp - sp.lo + content->second.base) & 0x3FFF;
            const bool four = t.psm == PSMT4 || t.psm == PSMT4HL || t.psm == PSMT4HH;
            const uint64_t key = mix(mix(sp.hash, uint64_t(own.cbp) | uint64_t(t.cpsm) << 16 | uint64_t(t.csm) << 24), four);
            if (setPalettes.size() > 8192 && !setPalettes.count(key)) setPalettes.clear();   // (a bound: contents come and go)
            std::vector<uint32_t>& entries = setPalettes[key];
            if (entries.empty()) {
                entries.resize(four ? 16 : 256);
                loadSetContent(sp.hash, content->second);
                assetMemory->readClut(own, entries.data());
            }
            mem.storeClut(t, entries.data());
        }
    }
    static const bool logLoads = std::getenv("GT4_GSN_XFER") != nullptr;
    if (logLoads) {
        const uint16_t* c = reinterpret_cast<const uint16_t*>(mem.clutBuffer());
        std::printf("[gsn clut] packet %u tbp %X cbp %X cpsm %X csm %u csa %u psm %X cld %u, entry 64 %04X%04X\n", packetsThisFrame - 1,
                    t.tbp, t.cbp, t.cpsm, t.csm, t.csa, t.psm, t.cld, c[256 + 64], c[64]);
    }
    clutSource = {};
    const PageOwner& page = pages[(t.cbp / 32) & 511];
    if (!page.target || (t.cpsm != PSMCT32 && t.cpsm != PSMCT24)) return;
    const Target& c = targets[page.target - 1];
    if (c.depth || !layout32(c.psm)) return;
    clutSource = {page.target, texture};
}

void GsFrontend::transferStarted(uint32_t dir) {
    const uint64_t b = reg[BITBLTBUF], p = reg[TRXPOS], s = reg[TRXREG];
    GsMemory::Transfer t;
    t.sbp = b & 0x3FFF;
    t.sbw = (b >> 16) & 0x3F;
    t.spsm = (b >> 24) & 0x3F;
    t.dbp = (b >> 32) & 0x3FFF;
    t.dbw = (b >> 48) & 0x3F;
    t.dpsm = (b >> 56) & 0x3F;
    t.ssax = p & 0x7FF;
    t.ssay = (p >> 16) & 0x7FF;
    t.dsax = (p >> 32) & 0x7FF;
    t.dsay = (p >> 48) & 0x7FF;
    t.dir = (p >> 59) & 3;
    t.rrw = s & 0xFFF;
    t.rrh = (s >> 32) & 0xFFF;
    imageActive = false;
    // GT4_GSN_XFER (debugging): every transfer.
    static const bool logTransfers = std::getenv("GT4_GSN_XFER") != nullptr;
    if (logTransfers)
        std::printf("[gsn xfer] dir %u src %X/%u psm %X (%u,%u) dst %X/%u psm %X (%u,%u) %ux%u\n", dir, t.sbp, t.sbw, t.spsm, t.ssax, t.ssay,
                    t.dbp, t.dbw, t.dpsm, t.dsax, t.dsay, t.rrw, t.rrh);
    if (dir == 0) {
        mem.beginWrite(t);
        imageActive = true;
        // The pages' generation moves only if the pixels change (imageWritten): GT4 sends the same
        // textures every frame, and they need not be decoded again.
        memoryWritten(t.dbp, t.dbw, t.dpsm, t.dsax, t.dsay, t.rrw, t.rrh, false);
        ++stats.uploads;
    } else if (dir == 2) {
        materializeRect(t.sbp, t.sbw, t.spsm, t.ssax, t.ssay, t.rrw, t.rrh, kForWrite);
        materializeRect(t.dbp, t.dbw, t.dpsm, t.dsax, t.dsay, t.rrw, t.rrh, kForWrite);
        stats.copyBytes += uint64_t(t.rrw) * t.rrh * psmBits(t.dpsm) / 8;
        mem.copy(t);   // a copy out of a render target is stale here (the target lives on the GPU)
        memoryWritten(t.dbp, t.dbw, t.dpsm, t.dsax, t.dsay, t.rrw, t.rrh);
        ++stats.copies;
    } else if (dir == 1) {
        readStarted(t);
    }
}

void GsFrontend::read(const gfx::ReadRecord& r) {
    GsMemory::Transfer t;
    t.sbp = r.base;
    t.sbw = r.width;
    t.spsm = r.format;
    t.ssax = r.x;
    t.ssay = r.y;
    t.rrw = r.w;
    t.rrh = r.h;
    t.dir = 0;
    static const bool logTransfers = std::getenv("GT4_GSN_XFER") != nullptr;
    if (logTransfers)
        std::printf("[gsn read] src %X/%u psm %X (%u,%u) %ux%u to %08X\n", t.sbp, t.sbw, t.spsm, t.ssax, t.ssay, t.rrw, t.rrh, r.destination);
    imageActive = false;
    readAsync = (r.qwords & gfx::kReadAsync) != 0;
    readStarted(t);
    readAsync = false;
}

// A GS -> host read starts: the packed pixels from the memory model, or from the GPU where a draw made them.
void GsFrontend::readStarted(const GsMemory::Transfer& t) {
    {
        materializeRect(t.sbp, t.sbw, t.spsm, t.ssax, t.ssay, t.rrw, t.rrh, kForRead);
        readData = mem.read(t);
        readPos = 0;
        // Whether a draw wrote any of the source pages after the last transfer into them.
        uint32_t pw, ph;
        pageSize(t.spsm, pw, ph);
        const uint32_t perRow = std::max<uint32_t>(1, (t.spsm == PSMT8 || t.spsm == PSMT4) ? t.sbw / 2 : t.sbw);
        readTarget = false;
        gpuReadPending = false;
        uint32_t owner = 0;
        bool oneOwner = true;
        for (uint32_t py = t.ssay / ph; py <= (t.ssay + std::max<uint32_t>(t.rrh, 1) - 1) / ph; ++py)
            for (uint32_t px = t.ssax / pw; px <= (t.ssax + std::max<uint32_t>(t.rrw, 1) - 1) / pw; ++px)
                for (uint32_t extra = 0; extra < 2; ++extra) {
                    if (extra && !(t.sbp % 32)) break;
                    const uint32_t page = pages[(t.sbp / 32 + py * perRow + px + extra) & 511].target;
                    if (!page) continue;
                    readTarget = true;
                    if (owner && page != owner) oneOwner = false;
                    owner = page;
                }
        ++stats.reads;
        if (readTarget) {
            ++stats.readsFromTargets;
            // GT4's reads (the car thumbnails it renders into 16-bit targets and keeps in EE memory, Photo Travel's
            // tiles it reads as CT24 from its CT32 frame buffer) are of one target in its own layout: those pixels
            // come from the GPU, packed in the read's format (CT24 and CT32 share a layout: the read leaves out
            // alpha). Anything else stays the memory model's (stale where drawn), counted by its two formats.
            markExact(owner);   // the game keeps these pixels: from now on drawn at the PS2's resolution
            Target& o = targets[owner - 1];
            const bool colour = t.spsm == PSMCT32 || t.spsm == PSMCT24 || t.spsm == PSMCT16 || t.spsm == PSMCT16S;
            const bool layout = o.psm == t.spsm || (o.psm == PSMCT32 && t.spsm == PSMCT24);
            if (oneOwner && !o.depth && o.bp == t.sbp && o.bw == t.sbw && colour && layout) {
                gpuRead = {owner, t.ssax, t.ssay, t.rrw, t.rrh, t.spsm, readAsync};
                gpuReadPending = true;
            } else {
                auto& kinds = stats.readMismatches;
                constexpr uint32_t kMax = Stats::kReadMismatchKinds;
                uint32_t k = 0;
                while (k < stats.readMismatchKindsSeen && !(kinds[k].targetPsm == o.psm && kinds[k].readPsm == t.spsm)) ++k;
                if (k == stats.readMismatchKindsSeen) {
                    if (k < kMax) {   // a new pair: logged once
                        std::fprintf(stderr, "gsn: a GS -> host read of drawn pixels (src %X/%u psm %X, target bp %X/%u psm %X%s) is "
                                     "served from the memory model, without what was drawn\n", t.sbp, t.sbw, t.spsm, o.bp, o.bw,
                                     o.psm, oneOwner ? "" : ", more than one target");
                        kinds[k].targetPsm = static_cast<uint8_t>(o.psm);
                        kinds[k].readPsm = static_cast<uint8_t>(t.spsm);
                        ++stats.readMismatchKindsSeen;
                    } else {
                        k = kMax - 1;   // past the table: counted with its last pair
                    }
                }
                ++kinds[k].count;
            }
        }
    }
}

bool GsFrontend::takeGpuRead(ReadRequest& request) {
    if (!gpuReadPending) return false;
    gpuReadPending = false;
    request = gpuRead;
    return true;
}

void GsFrontend::setReadData(std::vector<uint8_t> data) {
    readData = std::move(data);
    readPos = 0;
}

void GsFrontend::clearSceneJobs() {
    sceneJobs.clear();
    std::fill(sceneJobFirst.begin(), sceneJobFirst.end(), UINT32_MAX);
    std::fill(sceneJobLast.begin(), sceneJobLast.end(), UINT32_MAX);
}

void GsFrontend::cut(FrameList& out, bool async) {
    lastModelView = 0;   // (the split's draws: a new list starts its runs)
    clearSceneJobs();    // (B6: their indices are the list's that goes; a view with jobs in both is cut)
    sceneViewsFrom = 0;
    if (sceneOpen) {   // a partial list inside a scene frame (a.8): its views with tagged draws are cut
        ++sceneCuts;
        if (async) ++sceneCutsAsync;
        for (ViewBinding& v : viewBindings) {   // (a view that ended before the cut is not: its package is on the list)
            if (v.inList && !v.ended) v.cut = true;
            v.inList = false;
        }
    }
    lastReinterpretation = {};
    lastTexture.valid = false;
    lastMemoryReinterpretation = {};
    frame.targets = targets;
    frame.displayed = false;
    std::swap(out, frame);
    frame.clear();
}

void GsFrontend::readTransfer(void* out, uint32_t qwc) {
    auto* bytes = static_cast<uint8_t*>(out);
    const size_t want = size_t(qwc) * 16;
    const size_t have = readPos < readData.size() ? std::min(want, readData.size() - readPos) : 0;
    if (have) std::memcpy(bytes, readData.data() + readPos, have);
    std::memset(bytes + have, 0, want - have);
    readPos += want;
}

void GsFrontend::restoreMemory(const void* data) {
    lastTexture.valid = false;
    std::memcpy(mem.data(), data, GsMemory::kBytes);
    for (uint32_t page = 0; page < pages.size(); ++page) {
        pages[page] = {0, ++serial};
        memorySerial[page] = serial;
        ++pageGeneration[page];
    }
    ++uploads;
}

void GsFrontend::imageWritten(const uint8_t* bytes, size_t size) {
    stats.uploadBytes += size;
    if (!mem.write(bytes, size)) return;
    static const bool logChanges = std::getenv("GT4_GSN_XFER") != nullptr;
    if (logChanges) std::printf("[gsn changed] %zu bytes, %zu pages\n", size, uploadPages.size());
    for (const uint32_t page : uploadPages) ++pageGeneration[page];
    ++uploads;
}

void GsFrontend::memoryWritten(uint32_t bp, uint32_t bw, uint32_t psm, uint32_t x, uint32_t y, uint32_t w, uint32_t h, bool changed) {
    materializeRect(bp, bw, psm, x, y, w, h, kForWrite);   // (a write into part of a set's page keeps the rest)
    if (!changed) uploadPages.clear();
    uint32_t pw, ph;
    pageSize(psm, pw, ph);
    const uint32_t perRow = std::max<uint32_t>(1, (psm == PSMT8 || psm == PSMT4) ? bw / 2 : bw);
    for (uint32_t py = y / ph; py <= (y + std::max<uint32_t>(h, 1) - 1) / ph; ++py)
        for (uint32_t px = x / pw; px <= (x + std::max<uint32_t>(w, 1) - 1) / pw; ++px) {
            for (uint32_t extra = 0; extra < 2; ++extra) {   // a base that is not page-aligned spills into the next page
                const uint32_t page = (bp / 32 + py * perRow + px + extra) & 511;
                if (extra && !(bp % 32)) break;
                if (changed) {
                    ++pageGeneration[page];
                    ++uploads;
                } else {
                    uploadPages.push_back(page);
                }
                pages[page] = {0, ++serial};
                memorySerial[page] = serial;
            }
        }
}

void GsFrontend::kick(bool draw) {
    const uint32_t prim = primValue();
    const uint32_t type = prim & 7;
    const uint32_t need = type == 0 ? 1 : (type == 1 || type == 2 || type == 6) ? 2 : 3;
    ++queued;
    if (queued < need) return;
    Vertex v[3] = {queue[0], queue[1], queue[2]};
    switch (type) {
    case 0: if (draw) emit(v, 1, Topology::Points); queued = 0; break;
    case 1: if (draw) emit(v, 2, Topology::Lines); queued = 0; break;
    case 2:   // line strip: the second vertex starts the next line
        if (draw) emit(v, 2, Topology::Lines);
        queue[0] = queue[1];
        queued = 1;
        break;
    case 3: if (draw) emit(v, 3, Topology::Triangles); queued = 0; break;
    case 4:   // triangle strip
        if (draw) emit(v, 3, Topology::Triangles);
        queue[0] = queue[1];
        queue[1] = queue[2];
        queued = 2;
        break;
    case 5:   // triangle fan: the first vertex stays
        if (draw) emit(v, 3, Topology::Triangles);
        queue[1] = queue[2];
        queued = 2;
        break;
    case 6: {   // sprite: two corners, the second's colour, Z and fog
        if (draw) {
            const Vertex& a = v[0];
            const Vertex& b = v[1];
            Vertex c[6];
            auto corner = [&](Vertex& out, bool right, bool bottom) {
                out = b;
                out.x = right ? b.x : a.x;
                out.y = bottom ? b.y : a.y;
                out.s = right ? b.s : a.s;
                out.t = bottom ? b.t : a.t;
                out.q = b.q;
                out.u = right ? b.u : a.u;
                out.v = bottom ? b.v : a.v;
            };
            const bool fst = (primValue() >> 8) & 1;
            const float au = fst ? a.u : a.s / b.q, bu = fst ? b.u : b.s / b.q, av = fst ? a.v : a.t / b.q, bv = fst ? b.v : b.t / b.q;
            corner(c[0], false, false);
            corner(c[1], true, false);
            corner(c[2], false, true);
            corner(c[3], true, false);
            corner(c[4], true, true);
            corner(c[5], false, true);
            for (Vertex& k : c) {
                k.minU = std::min(au, bu);
                k.maxU = std::max(au, bu);
                k.minV = std::min(av, bv);
                k.maxV = std::max(av, bv);
            }
            emit(c, 3, Topology::Triangles);
            emit(c + 3, 3, Topology::Triangles);
        }
        queued = 0;
        break;
    }
    default: queued = 0; break;
    }
}

void GsFrontend::emit(const Vertex* v, uint32_t n, Topology topology) {
    const uint32_t prim = primValue();
    ++stats.primitives;
    // the drawing state: once while it is unchanged (a draw record's primitives, a packet's)
    EmitCache& cache = emitCache;
    const bool cached = cache.valid && cache.prim == prim && cache.serial == stateSerial && cache.frame == frameCount;
    if (!cached) {
        cache.state = currentState(prim);
        cache.valid = false;
    }
    const DrawState& s = cache.state;
    // Flat shading: the last vertex's colour for the whole primitive.
    Vertex out[3];
    for (uint32_t k = 0; k < n; ++k) {
        out[k] = v[k];
        if (!((prim >> 3) & 1)) out[k].rgba = v[n - 1].rgba;
    }
    // GT4's Z24 pass (Photo Travel's depth of field): white drawn into a Z buffer's memory as a frame of its format,
    // blended (Cs - Cd) x 128 / 128, turns every depth Z into max - Z, which the draws after it test with GEQUAL;
    // a second pass turns them back. Here the Z buffer is a depth image, which a colour draw cannot reach: the
    // inversion is kept as a state of the Z buffer instead (its draws' Z and tests turned round). One run of
    // these primitives (the pass's strips) is one inversion.
    {
        const uint32_t f = s.c.colorFormat;
        const bool zFrame = f == PSMZ32 || f == PSMZ24 || f == PSMZ16 || f == PSMZ16S;
        bool inverts = zFrame && !((prim >> 4) & 1) && ((prim >> 6) & 1) && s.c.blendA == 0 && s.c.blendB == 1 && s.c.blendC == 2 &&
                       s.c.blendD == 2 && s.c.blendFixed == 0x80 && (s.c.colorKeep & 0xFFFFFF) == 0;
        for (uint32_t k = 0; inverts && k < n; ++k) inverts = (out[k].rgba & 0xFFFFFF) == 0xFFFFFF;
        if (inverts) {
            if (const uint32_t id = surfaceTarget(SurfaceKey::of(s.c.colorBase, s.c.colorWidth, f, true))) {
                Target& t = targets[id - 1];
                // The run's first primitive drawn again starts a second pass: every pixel it covers turned round
                // twice, as on the GS (the replay's Dive view draws the pass twice in a row at the end of each frame;
                // counted once, the Z buffer was left inverted for its next scene, whose 3D draws do not turn their Z
                // round: every other pair of frames lost its whole scene).
                bool again = invertRun == t.id && invertFirstCount == n;
                for (uint32_t k = 0; again && k < n; ++k) again = invertFirst[2 * k] == out[k].x && invertFirst[2 * k + 1] == out[k].y;
                if (invertRun != t.id) {
                    invertFirstCount = n;
                    for (uint32_t k = 0; k < n; ++k) {
                        invertFirst[2 * k] = out[k].x;
                        invertFirst[2 * k + 1] = out[k].y;
                    }
                }
                if (again) {   // (a heuristic: logged, so a log shows it if another pass ever trips it)
                    static uint64_t passes = 0;
                    ++passes;
                    if ((passes & (passes - 1)) == 0)
                        std::fprintf(stderr, "gsn: Z24 pass at bp %X drew its first primitive again: a second inversion (%llu so far)\n", t.bp,
                                     static_cast<unsigned long long>(passes));
                }
                if (invertRun != t.id || again) {
                    t.inverted = !t.inverted;
                    lastTexture.valid = false;   // (a Z texture of this buffer, P2c, reads it turned round now)
                }
                invertRun = t.id;
                ++stats.draws;
                return;
            }
        }
        invertRun = 0;
    }
    // Targets: the frame buffer and, when the depth test is on, the Z buffer.
    const uint32_t scissorX0 = s.c.scissorX0, scissorX1 = s.c.scissorX1;
    float x0 = 1e30f, x1 = -1e30f, y0 = 1e30f, y1 = -1e30f;
    for (uint32_t k = 0; k < n; ++k) {   // (homogeneous vertices: the projected points; one behind the eye: the scissor)
        float x = out[k].x, y = out[k].y;
        if (out[k].w != 0.0f) {
            if (!(out[k].w > 0.0f)) {
                x0 = float(scissorX0); x1 = float(scissorX1 + 1); y0 = float(s.c.scissorY0); y1 = float(s.c.scissorY1 + 1);
                break;
            }
            x /= out[k].w;
            y /= out[k].w;
        }
        x0 = std::min(x0, x);
        x1 = std::max(x1, x);
        y0 = std::min(y0, y);
        y1 = std::max(y1, y);
    }
    const uint32_t left = static_cast<uint32_t>(std::clamp(x0, static_cast<float>(scissorX0), static_cast<float>(scissorX1)));
    // The last pixel covered: an edge on a pixel boundary covers the pixel before it.
    const uint32_t right = static_cast<uint32_t>(std::clamp(std::max(x0, std::ceil(x1) - 1.0f), static_cast<float>(scissorX0), static_cast<float>(scissorX1)));
    const uint32_t scissorY0 = s.c.scissorY0, scissorY1 = s.c.scissorY1;
    const uint32_t top = static_cast<uint32_t>(std::clamp(y0, static_cast<float>(scissorY0), static_cast<float>(scissorY1)));
    const uint32_t bottom = static_cast<uint32_t>(std::clamp(std::max(y0, std::ceil(y1) - 1.0f), static_cast<float>(scissorY0), static_cast<float>(scissorY1)));
    // A triangle whose bounds hold no GS pixel inside the scissor draws nothing on the GS (pixel k is covered
    // from y0 <= k < y1); at a higher render scale the render pixels between the GS's sample points would still
    // catch its edge (menu text parked just below the screen showed as a line of glyph tops). Only for 2D
    // (sprites, UV texturing): 3D meshes keep their slivers, which at a higher scale fill the cracks between pixels.
    const bool flat = (prim & 7) == 6 || ((prim >> 8) & 1);
    if (topology == Topology::Triangles && flat &&
        (std::max(std::ceil(x0), static_cast<float>(scissorX0)) > std::min(std::ceil(x1) - 1.0f, static_cast<float>(scissorX1)) ||
         std::max(std::ceil(y0), static_cast<float>(scissorY0)) > std::min(std::ceil(y1) - 1.0f, static_cast<float>(scissorY1))))
        return;
    const uint32_t width = s.c.colorWidth;
    // ZTE with ZTST = ALWAYS and ZMSK set (the GS wants ZTE on: games "switch Z off" this way) neither tests nor
    // writes Z: no Z buffer (GT4's composition into the field buffer named the 448-line scene's).
    if (!cached) {
        cache.colour = targetFor(s.c.colorBase, s.c.colorFormat, false, scissorY1 + 1, width);
        cache.zte = s.c.depthTest && !(s.c.depthFunc == 1 && !s.c.depthWrite);
        cache.depth = cache.zte ? targetFor(s.c.depthBase, s.c.depthFormat, true, scissorY1 + 1, width) : 0;
        cache.prim = prim;
        cache.serial = stateSerial;
        cache.frame = frameCount;
        cache.valid = true;
    }
    const uint32_t colour = cache.colour, depth = cache.depth;
    const bool zte = cache.zte;
    if (!colour || (zte && !depth)) return;   // past the target limit
    // A Z buffer inverted in place (above): the Z this draw tests and writes, turned round the same way.
    const bool depthInverted = depth && depth <= targets.size() && targets[depth - 1].inverted;
    if (depthInverted) {
        const uint32_t zf = targets[depth - 1].psm;
        const uint32_t most = zf == PSMZ32 ? 0xFFFFFFFFu : zf == PSMZ24 ? 0xFFFFFFu : 0xFFFFu;
        for (uint32_t k = 0; k < n; ++k)
            out[k].z = out[k].w != 0.0f ? std::bit_cast<uint32_t>(static_cast<float>(most) - std::bit_cast<float>(out[k].z))
                                        : most - std::min(out[k].z, most);
    }
    if (s.c.colorKeep != 0xFFFFFFFFu) markWritten(colour, s.c.colorBase, width, s.c.colorFormat, left, right, top, bottom);
    if (depth && s.c.depthWrite) markWritten(depth, s.c.depthBase, width, s.c.depthFormat, left, right, top, bottom);
    TextureSource texture;
    if ((prim >> 4) & 1) {
        // The last primitive's texture while nothing it depends on changed. A target's pixels too (GT4's post
        // effects draw grids of pieces from one target): when this draw does not write that target (or the one the
        // palette came from) and nothing else wrote it since.
        const LastTexture& m = lastTexture;
        auto writtenOf = [&](uint32_t id) -> uint64_t { return id && id < targetWritten.size() ? targetWritten[id] : 0; };
        if (m.valid && m.uploads == uploads && m.ownerChanges == ownerChanges && m.clutLoads == clutLoads && m.state == s &&
            writtenOf(m.sourceTarget) == m.sourceWritten && writtenOf(m.paletteTarget) == m.paletteWritten) {
            texture = m.source;
        } else {
            texture = textureFor(s);
            uint32_t source = 0, palette = 0;
            if (texture.kind == TextureSource::Kind::Target) {
                source = texture.id;
            } else if (texture.kind == TextureSource::Kind::Reinterpreted && texture.id < frame.reinterpretations.size()) {
                source = frame.reinterpretations[texture.id].target;
                palette = frame.reinterpretations[texture.id].clutTarget;
            }
            const bool pixels = texture.kind == TextureSource::Kind::Target || texture.kind == TextureSource::Kind::Reinterpreted;
            auto drawnHere = [&](uint32_t id) { return id && (id == colour || id == depth); };
            const bool keep = texture.kind == TextureSource::Kind::Memory || (pixels && !drawnHere(source) && !drawnHere(palette));
            lastTexture = {keep, s, texture, uploads, ownerChanges, clutLoads, source, writtenOf(source), palette, writtenOf(palette)};
        }
    }
    // A 3D scene's target read into another one (GT4's post effects, its composite into the display buffer)
    // is complete: the point for lighting it.
    if (texture.kind == TextureSource::Kind::Target && texture.id != colour) sceneRead(texture.id);
    if (depth && !((prim >> 8) & 1) && (prim & 7) != 6) {
        Scene& scene = scenes[colour];
        ++scene.draws;
        scene.depth = depth;
        scene.width = std::max<uint32_t>(scene.width, scissorX1 + 1);
        scene.height = std::max<uint32_t>(scene.height, scissorY1 + 1);
        if ((prim >> 5) & 1) {
            scene.fogColour = s.fogColor;
            scene.fogged = true;
        }
        const std::array<uint16_t, 4> rect = {static_cast<uint16_t>(scissorX0), static_cast<uint16_t>(scissorY0),
                                              static_cast<uint16_t>(scissorX1), static_cast<uint16_t>(scissorY1)};
        auto known = std::find_if(scene.viewports.begin(), scene.viewports.end(), [&](const auto& v) { return v.first == rect; });
        if (known != scene.viewports.end()) ++known->second;
        else if (scene.viewports.size() < 8) scene.viewports.push_back({rect, 1});
    }
    noteDraw(colour, depth, texture);
    uint8_t bracketRule = 0;
    const uint8_t bracket = bracketFor(colour, bracketRule);
    // Batch with the previous draw when nothing changed.
    if (!frame.commands.empty() && frame.commands.back().kind == FrameList::Command::Kind::Draw) {
        Draw& last = frame.draws[frame.commands.back().index];
        if (last.topology == topology && last.state == s && last.target == colour && last.depth == depth &&
            last.texture.kind == texture.kind && last.texture.id == texture.id && last.texture.version == texture.version &&
            last.depthInverted == depthInverted && last.bracket == bracket &&
            last.first + last.count == frame.vertices.size()) {
            frame.vertices.insert(frame.vertices.end(), out, out + n);
            last.count += n;
            return;
        }
    }
    Draw d;
    d.state = s;
    d.topology = topology;
    d.first = static_cast<uint32_t>(frame.vertices.size());
    d.count = n;
    d.target = colour;
    d.depth = depth;
    d.depthInverted = depthInverted;
    d.texture = texture;
    d.bracket = bracket;
    countBracket(bracketRule);
    frame.vertices.insert(frame.vertices.end(), out, out + n);
    frame.commands.push_back({FrameList::Command::Kind::Draw, static_cast<uint32_t>(frame.draws.size())});
    frame.draws.push_back(d);
    ++stats.draws;
}

// A draw into `colour` (depth-tested against `depth`) sampling `texture`: a target drawn at the PS2's resolution
// (Target::exact) passes that on to the target it samples and its Z buffer.
void GsFrontend::noteDraw(uint32_t colour, uint32_t depth, const TextureSource& texture) {
    if (!colour || colour > targets.size()) return;
    Target& c = targets[colour - 1];
    if (depth) c.lastDepth = depth;
    if (texture.kind != TextureSource::Kind::Target || !texture.id || texture.id == colour || texture.id > targets.size()) return;
    if (std::find(std::begin(c.sources), std::end(c.sources), texture.id) == std::end(c.sources)) {
        std::copy_backward(std::begin(c.sources), std::end(c.sources) - 1, std::end(c.sources));
        c.sources[0] = texture.id;
    }
    if (c.exact) markExact(texture.id);
}

// That target, its Z buffer and what its draws sampled, at the PS2's resolution from now on.
void GsFrontend::markExact(uint32_t target) {
    // (narrow ones only: the car pictures' chain is 64 to 128 wide; a screen-sized buffer they read keeps its scale)
    if (!target || target > targets.size() || targets[target - 1].exact || targets[target - 1].bw * 64 >= 512) return;
    // A Z buffer sampled into such a chain keeps the scale of the scene drawn with it (Photo Travel samples the
    // preview's Z into a narrow target: at the PS2's resolution under a finer colour buffer, it bounded the render
    // area of the depth-of-field pass to the top-left part). Sampled at either scale it gives the same depths.
    if (targets[target - 1].depth) return;
    Target& t = targets[target - 1];
    t.exact = true;
    if (t.lastDepth && t.lastDepth <= targets.size() && targets[t.lastDepth - 1].bw * 64 < 512) targets[t.lastDepth - 1].exact = true;
    const uint32_t sources[4] = {t.sources[0], t.sources[1], t.sources[2], t.sources[3]};
    for (const uint32_t s : sources) markExact(s);
}

// A target read as a texture into another: when it holds a 3D scene of the screen's size (64 depth-tested
// perspective draws or more since it was last lit; not the small environment and reflection maps), a Light
// command before this draw.
void GsFrontend::sceneRead(uint32_t target) {
    auto it = scenes.find(target);
    if (it == scenes.end() || it->second.draws < 64) return;
    for (const Target& t : targets) {
        if (t.id != target) continue;
        if (t.bw * 64 >= 512 && t.height >= 384) {
            frame.commands.push_back({FrameList::Command::Kind::Light, static_cast<uint32_t>(frame.lights.size())});
            FrameList::Lighting light{target, it->second.depth, it->second.width, it->second.height, it->second.fogColour, it->second.fogged};
            // The main view: the largest scissor; another, inside it and under half its area, is an inset view.
            auto area = [](const std::array<uint16_t, 4>& r) { return (uint32_t(r[2]) - r[0] + 1) * (uint32_t(r[3]) - r[1] + 1); };
            const auto& views = it->second.viewports;
            const auto main = std::max_element(views.begin(), views.end(), [&](const auto& a, const auto& b) { return area(a.first) < area(b.first); });
            for (const auto& v : views) {
                if (main == views.end() || v.first == main->first || area(v.first) * 2 > area(main->first)) continue;
                if (!light.hasInset) std::copy(v.first.begin(), v.first.end(), light.inset);
                light.inset[0] = std::min(light.inset[0], v.first[0]);
                light.inset[1] = std::min(light.inset[1], v.first[1]);
                light.inset[2] = std::max(light.inset[2], v.first[2]);
                light.inset[3] = std::max(light.inset[3], v.first[3]);
                light.hasInset = true;
            }
            frame.lights.push_back(light);
        }
        break;
    }
    it->second = {};
}

SurfaceKey SurfaceKey::of(uint32_t bp, uint32_t bw, uint32_t psm, bool depth) {
    SurfaceKey k;
    k.bp = bp;
    k.bw = bw;
    k.kind = depth ? ((psm & 2) ? kDepth16 : kDepth32) : layout32(psm) ? kColour32 : kColourOther;
    return k;
}

uint32_t GsFrontend::targetFor(uint32_t bp, uint32_t psm, bool depth, uint32_t heightNeeded, uint32_t bw) {
    const SurfaceKey key = SurfaceKey::of(bp, bw, psm, depth);
    if (const uint32_t id = surfaceTarget(key)) {
        Target& t = targets[id - 1];
        if (heightNeeded > t.height) t.height = (heightNeeded + 31) & ~31u;
        if (!depth) t.psm = psm;   // CT24 and CT32 share the layout; the last write tells which alpha is valid
        if (readBackShape(depth, psm, bw) && !t.exact) markExact(t.id);
        t.lastUsed = frameCount;
        return t.id;
    }
    // GT4 uses about twenty at a time, but some passes draw into many buffers once (the loading screen's photo
    // renders its tiles at a series of addresses): past the limit, the slot of the target unused for longest
    // (not in this frame or the last) is reused. Garbage registers (GT mode's map in the port, before a fix) made
    // thousands in one frame, each a GPU image at the render scale, and lost the device: with no slot free, the
    // draw is left out.
    constexpr size_t kMaxTargets = 96;
    Target* slot = nullptr;
    if (targets.size() >= kMaxTargets) {
        for (Target& candidate : targets)
            if (candidate.lastUsed + 2 <= frameCount && (!slot || candidate.lastUsed < slot->lastUsed)) slot = &candidate;
        if (!slot) {
            ++stats.droppedDraws;
            return 0;
        }
        // Forget what the old buffer owned: its pages are plain memory again, its surface no longer names it.
        surfaces.erase(slot->surface().packed());
        for (PageOwner& page : pages)
            if (page.target == slot->id) page = {};
        if (slot->id < targetWritten.size()) targetWritten[slot->id] = 0;
        if (clutSource.target == slot->id) clutSource = {};
        ++stats.targetsReused;
    }
    Target t;
    t.id = slot ? slot->id : static_cast<uint32_t>(targets.size()) + 1;
    t.generation = slot ? slot->generation + 1 : 0;
    t.lastUsed = frameCount;
    t.bp = bp;
    t.bw = bw;
    t.psm = psm;
    t.depth = depth;
    t.exact = readBackShape(depth, psm, bw);
    t.height = (std::max<uint32_t>(heightNeeded, 32) + 31) & ~31u;
    if (slot) *slot = t;
    else targets.push_back(t);
    surfaces[key.packed()] = t.id;
    return t.id;
}

// The pages under the rectangle (x0, y0)-(x1, y1) (inclusive): GT4 keeps palettes and index ramps beside the
// small areas it draws in, in the same buffer.
void GsFrontend::markWritten(uint32_t targetId, uint32_t bp, uint32_t bw, uint32_t psm, uint32_t x0, uint32_t x1, uint32_t y0,
                             uint32_t y1) {
    uint32_t pw, ph;
    pageSize(psm, pw, ph);
    const uint32_t perRow = std::max<uint32_t>(1, bw);
    const uint32_t first = std::min(x0 / pw, perRow - 1), last = std::min(x1 / pw, perRow - 1);
    for (uint32_t py = y0 / ph; py <= y1 / ph; ++py)
        for (uint32_t px = first; px <= last; ++px) {
            // (no set page into memory: a draw changes the page's owner, not memory, where the set's content stays
            // valid for whatever reads or writes memory there later, P2a)
            PageOwner& p = pages[(bp / 32 + py * perRow + px) & 511];
            if (p.target != targetId) ++ownerChanges;
            p = {targetId, ++serial};
        }
    if (targetWritten.size() <= targetId) targetWritten.resize(targetId + 1, 0);
    targetWritten[targetId] = serial;
}

namespace {
bool sameTexAlpha(const Reinterpretation& a, const Reinterpretation& b) {
    return a.texAlpha0 == b.texAlpha0 && a.texAlpha1 == b.texAlpha1 && a.texBlackTransparent == b.texBlackTransparent;
}
}  // namespace

// A page's content hash (64-bit words, multiply-xorshift), computed again only after a transfer wrote the page.
uint64_t GsFrontend::pageContent(uint32_t page) {
    page &= 511;
    materializePage(page, kForTexture);
    if (pageHashGeneration[page] == pageGeneration[page]) return pageHash[page];
    const uint8_t* bytes = mem.data() + size_t(page) * 8192;
    uint64_t h = 0x9E3779B97F4A7C15ull ^ page;
    for (size_t k = 0; k < 8192; k += 8) {
        uint64_t w;
        std::memcpy(&w, bytes + k, 8);
        h = (h ^ w) * 0xFF51AFD7ED558CCDull;
        h ^= h >> 29;
    }
    pageHash[page] = h;
    pageHashGeneration[page] = pageGeneration[page];
    return h;
}

void GsFrontend::texSet(const uint8_t* payload, size_t bytes) {
    gfx::TexSetRecord r;
    if (bytes < sizeof r) return;
    std::memcpy(&r, payload, sizeof r);
    const size_t head = sizeof r + r.uploads * sizeof(gfx::TexSetUpload);
    if (head > bytes) return;
    std::vector<gfx::TexSetUpload> uploads(r.uploads);
    std::memcpy(uploads.data(), payload + sizeof r, r.uploads * sizeof(gfx::TexSetUpload));
    const gfx::SetSlots::Slot slot = gfx::SetSlots::of(r, uploads.data());
    if (std::getenv("GT4_N3_SETLOG") && r.sent) {   // LOCAL N3 DEBUG: a sent set's uploads and first bytes (the locator)
        std::printf("N3 set: id %u hash %016llx lo %u uploads %u bytes %zu first", r.id, static_cast<unsigned long long>(slot.hash),
                    unsigned(slot.lo), unsigned(r.uploads), bytes - head);
        for (size_t q = 0; q < 32 && head + q < bytes; ++q) std::printf("%s%02x", q ? "" : " ", payload[head + q]);
        std::printf("\n");
        for (uint32_t k = 0; k < r.uploads; ++k)
            std::printf("N3 set upload: hash %016llx k %u bp %u bw %u psm %u w %u h %u qwords %u source %08x\n",
                        static_cast<unsigned long long>(slot.hash), k, uploads[k].destination & 0x3FFF, (uploads[k].destination >> 16) & 0x3F,
                        (uploads[k].destination >> 24) & 0x3F, unsigned(uploads[k].w), unsigned(uploads[k].h), uploads[k].qwords,
                        uploads[k].source);
    }
    if (!r.id) {   // a content kept with no set holding it (exportSets: a recording that starts mid-run)
        if (r.sent && !setContents.count(slot.hash)) {
            SetContent c;
            c.base = slot.lo;
            c.uploads = std::move(uploads);
            c.bytes.assign(payload + head, payload + bytes);
            setContentBytes += c.bytes.size();
            setContents.emplace(slot.hash, std::move(c));
        }
        setSlots.keep(slot.hash);
        return;
    }
    if (r.sent && !setContents.count(slot.hash)) {
        SetContent c;
        c.base = slot.lo;
        c.uploads = std::move(uploads);
        c.bytes.assign(payload + head, payload + bytes);
        setContentBytes += c.bytes.size();
        setContents.emplace(slot.hash, std::move(c));
    }
    setUploadsToo.clear();
    for (uint32_t k = 0; k < r.uploads; ++k) {
        gfx::TexSetUpload u;
        std::memcpy(&u, payload + sizeof r + k * sizeof u, sizeof u);
        setUploadsToo.emplace_back(u.source, u.destination & 0x3FFF);
    }
    // The pages its uploads cover: written now (as the PS2's uploads wrote them), holding this set; its bytes go
    // into graphics memory only where something reads or writes them there (materializePage).
    if (nativeTextures) {
        const auto* sent = payload + sizeof r;
        for (uint32_t k = 0; k < r.uploads; ++k) {
            gfx::TexSetUpload u;
            std::memcpy(&u, sent + k * sizeof u, sizeof u);
            const uint32_t bp = u.destination & 0x3FFF, bw = (u.destination >> 16) & 0x3F, psm = (u.destination >> 24) & 0x3F;
            uint32_t pw, ph;
            pageSize(psm, pw, ph);
            const uint32_t perRow = std::max<uint32_t>(1, (psm == PSMT8 || psm == PSMT4) ? bw / 2 : bw);
            for (uint32_t py = 0; py <= (std::max<uint32_t>(u.h, 1) - 1) / ph; ++py)
                for (uint32_t px = 0; px <= (std::max<uint32_t>(u.w, 1) - 1) / pw; ++px)
                    for (uint32_t extra = 0; extra < 2; ++extra) {
                        if (extra && !(bp % 32)) break;
                        const uint32_t page = (bp / 32 + py * perRow + px + extra) & 511;
                        pages[page] = {0, ++serial};
                        memorySerial[page] = serial;
                        ++pageGeneration[page];
                        setPages[page] = {slot.id, serial, false, slot.hash, slot.lo};
                    }
        }
    }
    // (the same lifetime as the runtime's: a content goes with the last set that holds it; pages that still hold
    // it get its bytes first)
    setSlots.add(slot, [this](uint64_t gone) {
        auto it = setContents.find(gone);
        if (it == setContents.end()) return;
        for (uint32_t page = 0; page < 512; ++page)
            if (setPages[page].hash == gone) materializePage(page, kForOther);
        setContentBytes -= it->second.bytes.size();
        if (assetLoaded == gone) assetLoaded = 0;
        setContents.erase(it);
    });
}

std::vector<GsFrontend::DrawnTarget> GsFrontend::drawnTargets() const {
    // (a target the game drew before a recording that starts mid-run, then only samples: a night sky drawn at the
    // race load. Without its pixels in the state the replay samples whatever the memory model held there)
    std::vector<DrawnTarget> out;
    for (const Target& t : targets) {
        if (t.depth || !t.id || !t.height) continue;
        uint32_t pw, ph;
        pageSize(t.psm, pw, ph);
        const uint32_t perRow = std::max<uint32_t>(1, t.bw), rows = (t.height + ph - 1) / ph;
        DrawnTarget d;
        for (uint32_t p = 0; p < perRow * rows; ++p) {
            const uint32_t page = (t.bp / 32 + p) & 511;
            if (pages[page].target == t.id) d.pages.push_back(static_cast<uint16_t>(page));
        }
        if (d.pages.empty()) continue;
        d.read.target = t.id;
        d.read.width = perRow * 64;
        d.read.height = t.height;
        d.read.psm = t.psm;
        d.bp = t.bp;
        d.bw = t.bw;
        out.push_back(d);
    }
    return out;
}

size_t GsFrontend::exportSets(const std::function<void(const gfx::TexSetRecord&, const std::vector<gfx::TexSetUpload>&,
                                                     const std::vector<uint8_t>&)>& out) const {
    // the contents kept with no set holding them (id 0, in the order they went), then each set in graphics memory
    // with its uploads at its slot; a content's bytes with the first set that holds it
    std::unordered_set<uint64_t> exported;
    static const std::vector<uint8_t> none;
    for (const uint64_t hash : setSlots.kept()) {
        auto c = setContents.find(hash);
        if (c == setContents.end()) continue;
        gfx::TexSetRecord r{};
        r.hashLow = static_cast<uint32_t>(hash);
        r.hashHigh = static_cast<uint32_t>(hash >> 32);
        r.uploads = static_cast<uint16_t>(c->second.uploads.size());
        r.sent = exported.insert(hash).second ? 1u : 0u;
        out(r, c->second.uploads, r.sent ? c->second.bytes : none);
    }
    size_t missing = 0;
    for (const gfx::SetSlots::Slot& slot : setSlots.all()) {
        auto c = setContents.find(slot.hash);
        if (c == setContents.end()) {
            ++missing;
            continue;
        }
        gfx::TexSetRecord r{};
        r.id = slot.id;
        r.hashLow = static_cast<uint32_t>(slot.hash);
        r.hashHigh = static_cast<uint32_t>(slot.hash >> 32);
        r.uploads = static_cast<uint16_t>(c->second.uploads.size());
        r.size = static_cast<uint16_t>(slot.hi - slot.lo);
        r.sent = exported.insert(slot.hash).second ? 1u : 0u;
        std::vector<gfx::TexSetUpload> uploads = c->second.uploads;
        for (gfx::TexSetUpload& u : uploads)
            u.destination = (u.destination & ~0x3FFFu) | (((u.destination & 0x3FFF) - c->second.base + slot.lo) & 0x3FFF);
        out(r, uploads, r.sent ? c->second.bytes : none);
    }
    return missing;
}

// A set's content into assetMemory at the slot it was first sent at (once while it stays there).
namespace {
// Bytes hashed (64-bit words; a set's uploads are whole qwords).
uint64_t hashBytes(const uint8_t* p, size_t n) {
    uint64_t h = 0xCBF29CE484222325ull ^ n;
    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        uint64_t w;
        std::memcpy(&w, p + i, 8);
        h = (h ^ w) * 0x100000001B3ull;
        h ^= h >> 29;
    }
    for (; i < n; ++i) h = (h ^ p[i]) * 0x100000001B3ull;
    return h;
}
// The pages a rectangle at block `bp` of a buffer `bw` wide (64-pixel units) in format `psm` covers: first and last,
// unwrapped (textureFor's walk, as a range).
void pageRange(uint32_t bp, uint32_t bw, uint32_t psm, uint32_t w, uint32_t h, uint32_t& first, uint32_t& last) {
    uint32_t pw, ph;
    pageSize(psm, pw, ph);
    const uint32_t perRow = std::max<uint32_t>(1, (psm == PSMT8 || psm == PSMT4) ? bw / 2 : bw);
    const uint32_t rows = std::max<uint32_t>(1, (h + ph - 1) / ph), columns = std::max<uint32_t>(1, std::min<uint32_t>(perRow, (w + pw - 1) / pw));
    first = bp / 32;
    last = first + (rows - 1) * perRow + (columns - 1) + (bp % 32 ? 1 : 0);
}
}  // namespace

// A texture's image in a set's content: the uploads over its pages (their bytes and where they went). Two sets that
// differ elsewhere (an animated palette's upload) give the texture the same key, and its decode is kept.
uint64_t GsFrontend::imageKey(SetContent& sc, uint32_t firstPage, uint32_t lastPage) {
    if (sc.spans.size() != sc.uploads.size()) {
        sc.spans.clear();
        size_t at = 0;
        for (const gfx::TexSetUpload& u : sc.uploads) {
            SetContent::Span s{};
            pageRange(u.destination & 0x3FFF, (u.destination >> 16) & 0x3F, (u.destination >> 24) & 0x3F, u.w, u.h, s.first, s.last);
            const size_t n = std::min<size_t>(size_t(u.qwords) * 16, sc.bytes.size() - std::min(at, sc.bytes.size()));
            s.hash = hashBytes(sc.bytes.data() + std::min(at, sc.bytes.size()), n);
            at += size_t(u.qwords) * 16;
            sc.spans.push_back(s);
        }
    }
    uint64_t key = 0x1A6Eu;
    const bool wraps = lastPage > 511;
    for (size_t k = 0; k < sc.uploads.size(); ++k) {
        const SetContent::Span& s = sc.spans[k];
        if (!wraps && s.last <= 511 && (s.last < firstPage || s.first > lastPage)) continue;   // (wrapping: kept)
        const gfx::TexSetUpload& u = sc.uploads[k];
        key = mix(mix(key, s.hash), uint64_t(u.destination) | uint64_t(u.w) << 32 | uint64_t(u.h) << 48);
    }
    return key;
}

void GsFrontend::loadSetContent(uint64_t hash, const SetContent& sc) {
    if (assetLoaded == hash) return;
    if (!assetMemory) assetMemory = std::make_unique<GsMemory>();
    size_t at = 0;
    for (const gfx::TexSetUpload& u : sc.uploads) {
        GsMemory::Transfer tr;
        tr.dbp = u.destination & 0x3FFF;
        tr.dbw = (u.destination >> 16) & 0x3F;
        tr.dpsm = (u.destination >> 24) & 0x3F;
        tr.rrw = u.w;
        tr.rrh = u.h;
        const size_t n = std::min<size_t>(size_t(u.qwords) * 16, sc.bytes.size() - std::min(at, sc.bytes.size()));
        assetMemory->beginWrite(tr);
        if (n) assetMemory->write(sc.bytes.data() + at, n);
        at += size_t(u.qwords) * 16;
    }
    assetLoaded = hash;
}

void GsFrontend::materializePage(uint32_t page, MaterializeCause cause) {
    page &= 511;
    SetPage& sp = setPages[page];
    if (!sp.id || sp.materialized || memorySerial[page] != sp.serial) return;
    sp.materialized = true;
    auto content = setContents.find(sp.hash);
    if (content == setContents.end()) return;
    GT4_CPU_SCOPE("front end: set pages into memory");
    // (slots are whole pages, 004AAA20: a page of the slot is a page of the content where it was first sent)
    const uint32_t from = (page - (sp.lo - content->second.base) / 32) & 511;
    // the content's pages, kept (the slot cache brings the same sets back every frame, elsewhere)
    const uint64_t key = mix(sp.hash, from);
    if (setPageBytes.size() > 2048 && !setPageBytes.count(key)) setPageBytes.clear();   // (16 MiB)
    std::vector<uint8_t>& bytes = setPageBytes[key];
    if (bytes.empty()) {
        loadSetContent(sp.hash, content->second);
        bytes.assign(assetMemory->data() + size_t(from) * 8192, assetMemory->data() + size_t(from + 1) * 8192);
    }
    std::memcpy(mem.data() + size_t(page) * 8192, bytes.data(), 8192);
    ++stats.setPagesWritten;
    ++stats.setPagesByCause[cause];
}

void GsFrontend::materializeRect(uint32_t bp, uint32_t bw, uint32_t psm, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                 MaterializeCause cause) {
    uint32_t pw, ph;
    pageSize(psm, pw, ph);
    const uint32_t perRow = std::max<uint32_t>(1, (psm == PSMT8 || psm == PSMT4) ? bw / 2 : bw);
    for (uint32_t py = y / ph; py <= (y + std::max<uint32_t>(h, 1) - 1) / ph; ++py)
        for (uint32_t px = x / pw; px <= (x + std::max<uint32_t>(w, 1) - 1) / pw; ++px)
            for (uint32_t extra = 0; extra < 2; ++extra) {
                if (extra && !(bp % 32)) break;
                materializePage(bp / 32 + py * perRow + px + extra, cause);
            }
}

// The CLUT buffer's entries an indexed texture reads (a 4-bit one: 16 at CSA), hashed.
uint64_t GsFrontend::paletteHash(const GsMemory::Tex0& t) {
    const uint16_t* buf = reinterpret_cast<const uint16_t*>(mem.clutBuffer());
    const bool four = t.psm == PSMT4 || t.psm == PSMT4HL || t.psm == PSMT4HH;
    const bool ct32 = t.cpsm == PSMCT32 || t.cpsm == PSMCT24;
    const uint32_t first = four ? (ct32 ? (t.csa & 15) * 16 : t.csa * 16) : 0, count = four ? 16 : 256;
    // (the CLUT buffer changes only with a load: the last few hashes, by the loads so far)
    const uint32_t which = first | count << 10 | uint32_t(ct32) << 20;
    PaletteMemo& memo = paletteMemo[(which * 2654435761u >> 28) & 15];
    if (memo.loads == clutLoads && memo.which == which) return memo.hash;
    uint64_t h = 0x9A1E77E5ull;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t e = (first + i) & (ct32 ? 255 : 511);
        h = mix(h, ct32 ? (uint64_t(buf[e]) | uint64_t(buf[256 + e]) << 16) : buf[e]);
    }
    memo = {clutLoads, which, h};
    return h;
}

// A texture of a native set: decoded once per content from the set's own uploads (the PS2's texture format read
// as an asset: GT4 keeps 8- and 4-bit textures as 32-bit blocks), then a texture like any other. False when the set
// or its content is unknown, or its slot was written since (the caller goes on by graphics-memory address).
bool GsFrontend::setTexture(const DrawState& s, TextureSource& src) {
    const gfx::SetSlots::Slot* slot = setSlots.find(s.c.texture.set);
    if (!slot || !holdsSet(s.c.texture.base, slot->id)) return false;
    const uint64_t hash = slot->hash;
    auto content = setContents.find(hash);
    if (content == setContents.end()) return false;
    SetContent& sc = content->second;
    // The draw's texture moved from the set's slot to where the content was first sent: the image, the palette and
    // the mip levels (all inside the set).
    const int32_t delta = int32_t(slot->lo) - int32_t(sc.base);
    GsMemory::Tex0 t = GsMemory::Tex0::of(s.c.texture);
    t.tbp = uint32_t(int32_t(t.tbp) - delta) & 0x3FFF;
    t.cbp = uint32_t(int32_t(t.cbp) - delta) & 0x3FFF;
    gfx::Context rc;
    rc.sampler = s.c.sampler;
    if (!rc.sampler.mipAuto)
        for (uint32_t k = 0; k < 6; ++k)
            if (rc.sampler.mipBase[k]) rc.sampler.mipBase[k] = uint16_t(uint32_t(int32_t(rc.sampler.mipBase[k]) - delta) & 0x3FFF);
    // (the image: imageKey below, not the whole set's hash)
    uint64_t key = mix(mix(0x5E7u, uint64_t(t.tbp) | uint64_t(t.tbw) << 14 | uint64_t(t.psm) << 20 | uint64_t(t.tw) << 26 | uint64_t(t.th) << 30),
                       uint64_t(t.cbp) | uint64_t(t.cpsm) << 14 | uint64_t(t.csm) << 18 | uint64_t(t.csa) << 19);
    for (uint32_t k = 0; k < 6; ++k) key = mix(key, uint64_t(rc.sampler.mipBase[k]) | uint64_t(rc.sampler.mipWidth[k]) << 16);
    const gfx::Sampler& sm = rc.sampler;
    const bool indexed = t.psm == PSMT8 || t.psm == PSMT4 || t.psm == PSMT8H || t.psm == PSMT4HL || t.psm == PSMT4HH;
    const bool texaUsed = t.psm == PSMCT24 || t.psm == PSMCT16 || t.psm == PSMCT16S || (indexed && t.cpsm != PSMCT32);
    const Texa texa{s.texAlpha0, s.texAlpha1, s.texBlackTransparent != 0};
    key = mix(key, texaUsed ? (uint64_t(texa.ta0) | uint64_t(texa.ta1) << 8 | uint64_t(texa.aem) << 16) : 0);
    if (indexed) key = mix(key, paletteHash(t));
    // its mipmap levels (textureFor's rules)
    const uint32_t w = 1u << std::min<uint32_t>(t.tw, 10), h = 1u << std::min<uint32_t>(t.th, 10);
    struct Level {
        uint32_t tbp, tbw, w, h;
    };
    std::array<Level, 7> levels{};
    levels[0] = {t.tbp, t.tbw, w, h};
    uint32_t levelCount = 1;
    bool mipLevels = sm.mipAuto;
    for (uint32_t k = 0; k < 6; ++k) mipLevels = mipLevels || sm.mipBase[k] || sm.mipWidth[k];
    if (mipLevels && sm.minFilter >= 2) {
        for (uint32_t i = 1; i <= std::min<uint32_t>(sm.mipMax, 6); ++i) {
            const Level& prev = levels[i - 1];
            if (prev.w == 1 && prev.h == 1) break;
            Level& l = levels[i];
            l.w = std::max<uint32_t>(prev.w / 2, 1);
            l.h = std::max<uint32_t>(prev.h / 2, 1);
            if (sm.mipAuto && i <= 3) {
                l.tbp = prev.tbp + prev.w * prev.h * psmBits(t.psm) / 8 / 256;
                l.tbw = std::max<uint32_t>(prev.tbw / 2, 1);
            } else {
                l.tbp = sm.mipBase[i - 1];
                l.tbw = sm.mipWidth[i - 1];
            }
            levelCount = i + 1;
        }
    }
    key = mix(key, levelCount);
    {
        uint32_t first = ~0u, last = 0;
        for (uint32_t i = 0; i < levelCount; ++i) {
            uint32_t f, l;
            pageRange(levels[i].tbp, levels[i].tbw, t.psm, levels[i].w, levels[i].h, f, l);
            first = std::min(first, f);
            last = std::max(last, l);
        }
        key = mix(key, imageKey(sc, first, last));
    }
    Decoded& d = decoded[key];
    d.lastUsed = frameCount;
    // Its palette: the CLUT buffer as the draw has it.
    auto loadPalette = [&] { assetMemory->setClutBuffer(mem.clutBuffer()); };
    // The texture pack's image for it (named as PCSX2 names the texture), once; until loaded, the decoded texture.
    if (replacementsOn) {
        if (!d.named) {
            loadSetContent(hash, sc);
            if (indexed) loadPalette();
            MipAddress mips[6];
            for (uint32_t i = 1; i < levelCount; ++i) mips[i - 1] = {levels[i].tbp, levels[i].tbw};
            GT4_CPU_SCOPE("front end: texture pack naming");
            d.found = findTextureName(*assetMemory, t, texa, sm, mips, levelCount - 1, *texturePack, d.name, d.origin);
            d.named = true;
            ++stats.named;
            stats.found += d.found;
        }
        if (d.found && !d.replacedId) {
            const uint64_t loads = texturePack->loadedSerial();
            if (loads != d.packLoads) {
                d.packLoads = loads;
                if (auto image = texturePack->get(d.name)) d.replacedId = replacementTexture(d.name, *image);
            }
        }
        if (d.replacedId) {
            auto kept = replacementTextures.find(d.name);
            if (kept != replacementTextures.end() && kept->second.id == d.replacedId) {
                kept->second.lastUsed = frameCount;
                src.kind = TextureSource::Kind::Memory;
                src.id = d.replacedId;
                src.version = 1;
                src.regionX = static_cast<uint16_t>(d.origin[0]);
                src.regionY = static_cast<uint16_t>(d.origin[1]);
                src.regionWidth = static_cast<uint16_t>(d.name.regionWidth);
                src.regionHeight = static_cast<uint16_t>(d.name.regionHeight);
                ++stats.setTextures;
                return true;
            }
            d.replacedId = 0;
            d.packLoads = ~0ull;
        }
    }
    if (!d.id) {
        loadSetContent(hash, sc);
        if (indexed) loadPalette();
        d.id = nextTexture++;
        setTextureIdentity(d.id, key);   // (B6: a material keys on it)
        if (std::getenv("GT4_N3_SETLOG"))   // LOCAL N3 DEBUG: the texture's place in its set (the locator)
            std::printf("N3 texid: id %u key %016llx set %016llx setbase %u tbp %u cbp %u psm %u cpsm %u tw %u th %u\n", d.id,
                        static_cast<unsigned long long>(key), static_cast<unsigned long long>(hash), unsigned(sc.base),
                        unsigned(t.tbp), unsigned(t.cbp), unsigned(t.psm), unsigned(t.cpsm), unsigned(t.tw), unsigned(t.th));
        d.texels = 0;
        TextureImage image;
        image.id = d.id;
        image.version = 1;
        image.width = w;
        image.height = h;
        image.levels = levelCount;
        size_t texels = 0;
        for (uint32_t i = 0; i < levelCount; ++i) texels += size_t(levels[i].w) * levels[i].h;
        image.pixels.resize(texels);
        size_t at = 0;
        for (uint32_t i = 0; i < levelCount; ++i) {
            GsMemory::Tex0 lt = t;
            lt.tbp = levels[i].tbp;
            lt.tbw = levels[i].tbw;
            assetMemory->decode(lt, texa, 0, 0, levels[i].w, levels[i].h, image.pixels.data() + at);
            at += size_t(levels[i].w) * levels[i].h;
        }
        d.texels = texels;
        cachedTexels += texels;
        summarize(key, image);   // (F2b: the glow classifier's input; before the pixels go)
        frame.textures.push_back(std::move(image));
        ++stats.setDecodes;
    }
    src.kind = TextureSource::Kind::Memory;
    src.id = d.id;
    src.version = 1;
    ++stats.setTextures;
    return true;
}

TextureSource GsFrontend::textureFor(const DrawState& s) {
    GT4_CPU_SCOPE("front end: texture lookup");
    // A native set's texture (docs/NATIVE_V2.md).
    if (s.c.texture.set && nativeTextures) {
        TextureSource src;
        if (setTexture(s, src)) return src;
        ++stats.setMisses;
    }
    ++stats.byAddress;
    const GsMemory::Tex0 t = GsMemory::Tex0::of(s.c.texture);
    const uint32_t w = 1u << std::min<uint32_t>(t.tw, 10), h = 1u << std::min<uint32_t>(t.th, 10);
    uint32_t pw, ph;
    pageSize(t.psm, pw, ph);
    const uint32_t perRow = std::max<uint32_t>(1, (t.psm == PSMT8 || t.psm == PSMT4) ? t.tbw / 2 : t.tbw);
    // The most recent writer of the pages the texture covers.
    uint32_t owner = 0;
    uint64_t newest = 0, generation = 0, content = 0;
    const uint32_t rows = (h + ph - 1) / ph, columns = std::min<uint32_t>(perRow, (w + pw - 1) / pw);
    const auto eachPage = [&](auto&& f) {
        for (uint32_t py = 0; py < rows; ++py)
            for (uint32_t px = 0; px < std::max<uint32_t>(columns, 1); ++px)
                for (uint32_t extra = 0; extra < 2; ++extra) {
                    if (extra && !(t.tbp % 32)) break;
                    f((t.tbp / 32 + py * perRow + px + extra) & 511);
                }
    };
    eachPage([&](uint32_t page) {
                generation += pageGeneration[page] * (page + 1);
                content = mix(content, page);
                if (pages[page].target && pages[page].serial > newest) {
                    newest = pages[page].serial;
                    owner = pages[page].target;
                }
    });
    // The same overrun the other way: an image uploaded at the texture's base page after the newest target
    // write in its extent is what the texture reads, and the declared size runs past it into an older target
    // (the dealers' backdrop: a 640 x 448 T8 picture declared 1024 x 512, sent every frame into the memory the
    // previous frame's final 32-bit target used).
    if (owner && !pages[(t.tbp / 32) & 511].target && pages[(t.tbp / 32) & 511].serial > newest) owner = 0;
    // A target that starts at the texture's own base page wins: textures are declared at powers of two, so
    // their extent often runs past the buffer they read into later targets.
    // Several targets can start at one page (GT4 draws a 128-wide environment map and its 640-wide scene at
    // the same address): the most recently written one of the texture's width, as long as nothing wrote that
    // page after it (a menu's old 128-wide target at the race's frame buffer).
    if (!(t.tbp % 32)) {
        uint64_t best = 0;
        const uint64_t baseWritten = pages[(t.tbp / 32) & 511].serial;
        for (const SurfaceKey::Kind kind : {SurfaceKey::kColour32, SurfaceKey::kColourOther}) {
            const uint32_t id = surfaceTarget({t.tbp, t.tbw, kind});
            if (!id) continue;
            const Target& candidate = targets[id - 1];
            const uint64_t written = candidate.id < targetWritten.size() ? targetWritten[candidate.id] : 0;
            if (written && written >= baseWritten && written >= best) {
                best = written;
                owner = candidate.id;
            }
        }
    }
    TextureSource src;
    if (owner) {
        Target& target = targets[owner - 1];
        target.lastUsed = frameCount;
        const bool sameLayout = !target.depth && layout32(target.psm) == layout32(t.psm) &&
                                (t.psm == PSMCT32 || t.psm == PSMCT24 || t.psm == PSMCT16 || t.psm == PSMCT16S);
        if (sameLayout && target.bw == t.tbw && t.tbp >= target.bp && (t.tbp - target.bp) % 32 == 0) {
            const uint32_t page = (t.tbp - target.bp) / 32;
            src.kind = TextureSource::Kind::Target;
            src.id = owner;
            src.offsetX = static_cast<int32_t>((page % std::max<uint32_t>(1, target.bw)) * pw);
            src.offsetY = static_cast<int32_t>((page / std::max<uint32_t>(1, target.bw)) * ph);
            ++stats.targetReads;
            return src;
        }
        // Another format over a 32-bit frame: built on the GPU from the target (indexed formats).
        const bool indexedRead = t.psm == PSMT8 || t.psm == PSMT4 || t.psm == PSMT8H || t.psm == PSMT4HL || t.psm == PSMT4HH;
        if (!target.depth && layout32(target.psm) && indexedRead) {
            Reinterpretation r;
            r.target = owner;
            r.targetBp = target.bp;
            r.targetBw = target.bw;
            r.targetPsm = target.psm;
            r.texture = s.c.texture;
            r.texAlpha0 = s.texAlpha0;
            r.texAlpha1 = s.texAlpha1;
            r.texBlackTransparent = s.texBlackTransparent;
            r.width = w;
            r.height = h;
            std::memcpy(r.clut.data(), mem.clutBuffer(), r.clut.size());
            if (clutSource.target && samePalette(clutSource.texture, s.c.texture)) {
                const Target& c = targets[clutSource.target - 1];
                r.clutTarget = c.id;
                r.clutTargetBp = c.bp;
                r.clutTargetBw = c.bw;
            }
            src.kind = TextureSource::Kind::Reinterpreted;
            // The same as the previous one while the target has not been drawn into since: reused.
            const LastReinterpretation& last = lastReinterpretation;
            if (last.index < frame.reinterpretations.size()) {
                const Reinterpretation& p = frame.reinterpretations[last.index];
                const uint64_t written = owner < targetWritten.size() ? targetWritten[owner] : 0;
                if (p.target == r.target && p.texture == r.texture && sameTexAlpha(p, r) && p.clut == r.clut && p.clutTarget == r.clutTarget &&
                    written <= last.serial && (!r.clutTarget || r.clutTarget >= targetWritten.size() || targetWritten[r.clutTarget] <= last.serial)) {
                    src.id = last.index;
                    return src;
                }
            }
            src.id = static_cast<uint32_t>(frame.reinterpretations.size());
            frame.reinterpretations.push_back(r);
            lastReinterpretation = {src.id, serial};
            ++stats.reinterpreted;
            return src;
        }
        // A Z buffer read as a Z-format texture of its own layout (P2c: race7's pre-race frames copy a field's Z this
        // way): built on the GPU from the depth image. Z32 and Z24 share a layout; Z16 and Z16S each have their own.
        const bool zTexture = t.psm == PSMZ32 || t.psm == PSMZ24 || t.psm == PSMZ16 || t.psm == PSMZ16S;
        const bool zLayout = (t.psm == PSMZ32 || t.psm == PSMZ24) ? (target.psm == PSMZ32 || target.psm == PSMZ24) : t.psm == target.psm;
        if (target.depth && zTexture && zLayout && target.bw == t.tbw && t.tbp >= target.bp && (t.tbp - target.bp) % 32 == 0) {
            Reinterpretation r;
            r.target = owner;
            r.targetBp = target.bp;
            r.targetBw = target.bw;
            r.targetPsm = target.psm;
            r.texture = s.c.texture;
            r.texAlpha0 = s.texAlpha0;
            r.texAlpha1 = s.texAlpha1;
            r.texBlackTransparent = s.texBlackTransparent;
            r.width = w;
            r.height = h;
            r.depth = true;
            r.depthInverted = target.inverted;
            src.kind = TextureSource::Kind::Reinterpreted;
            // The same as the previous one while the Z buffer has not been drawn into or turned round since: reused.
            const LastReinterpretation& last = lastReinterpretation;
            if (last.index < frame.reinterpretations.size()) {
                const Reinterpretation& p = frame.reinterpretations[last.index];
                const uint64_t written = owner < targetWritten.size() ? targetWritten[owner] : 0;
                if (p.depth && p.target == r.target && p.texture == r.texture && sameTexAlpha(p, r) &&
                    p.depthInverted == r.depthInverted && written <= last.serial) {
                    src.id = last.index;
                    return src;
                }
            }
            src.id = static_cast<uint32_t>(frame.reinterpretations.size());
            frame.reinterpretations.push_back(std::move(r));
            lastReinterpretation = {src.id, serial};
            ++stats.reinterpreted;
            ++stats.depthTextures;
            return src;
        }
        src.reinterpreted = true;
        ++stats.reinterpreted;
        ++stats.readFallbacks;   // (P2: counted apart, 0 expected)
        // GT4_GSN_FALLBACKS=N (debugging): the first N, with the texture and the target it overlaps
        static const long fallbackLog = std::getenv("GT4_GSN_FALLBACKS") ? std::strtol(std::getenv("GT4_GSN_FALLBACKS"), nullptr, 10) : 0;
        if (static_cast<long>(stats.readFallbacks) <= fallbackLog)
            std::printf("[gsn fallback] frame %llu tex %X/%u psm %X %ux%u cbp %X: target %u bp %X/%u psm %X depth %d h %u\n",
                        static_cast<unsigned long long>(frameCount), t.tbp, t.tbw, t.psm, w, h, t.cbp, owner, target.bp, target.bw,
                        target.psm, target.depth ? 1 : 0, target.height);
    }
    // From here the texture is read from memory by address: the set pages it covers go into memory now (P2a: a
    // target's pixels and its reinterpretations, above, are the GPU's and never needed them).
    eachPage([&](uint32_t page) { materializePage(page, kForTexture); });
    const bool indexed = t.psm == PSMT8 || t.psm == PSMT4 || t.psm == PSMT8H || t.psm == PSMT4HL || t.psm == PSMT4HH;
    // Indices in memory, palette drawn on the GPU (GT4 writes its colour-graded pixels back this way).
    if (indexed && clutSource.target && samePalette(clutSource.texture, s.c.texture)) {
        const Target& c = targets[clutSource.target - 1];
        Reinterpretation r;
        r.texture = s.c.texture;
        r.texAlpha0 = s.texAlpha0;
        r.texAlpha1 = s.texAlpha1;
        r.texBlackTransparent = s.texBlackTransparent;
        r.width = w;
        r.height = h;
        r.indices.resize(size_t(w) * h);
        mem.indices(t, w, h, r.indices.data());
        std::memcpy(r.clut.data(), mem.clutBuffer(), r.clut.size());
        r.clutTarget = c.id;
        r.clutTargetBp = c.bp;
        r.clutTargetBw = c.bw;
        src.kind = TextureSource::Kind::Reinterpreted;
        // The same as the previous one (a sprite's second triangle) while neither the palette's target nor
        // GS memory changed since: reused.
        const LastReinterpretation& last = lastMemoryReinterpretation;
        if (last.index < frame.reinterpretations.size()) {
            const Reinterpretation& p = frame.reinterpretations[last.index];
            const uint64_t written = c.id < targetWritten.size() ? targetWritten[c.id] : 0;
            if (!p.target && p.texture == r.texture && sameTexAlpha(p, r) && p.clutTarget == r.clutTarget && written <= last.serial &&
                uploads == last.uploads) {
                src.id = last.index;
                return src;
            }
        }
        src.id = static_cast<uint32_t>(frame.reinterpretations.size());
        frame.reinterpretations.push_back(std::move(r));
        lastMemoryReinterpretation = {src.id, serial, uploads};
        ++stats.reinterpreted;
        return src;
    }
    // Mipmap levels (TEX1 MXL, MIPTBP1/2, or with MTBA levels 1-3 placed after each other), each half the
    // size of the one before.
    struct Level {
        uint32_t tbp, tbw, w, h;
    };
    std::array<Level, 7> levels{};
    levels[0] = {t.tbp, t.tbw, w, h};
    uint32_t levelCount = 1;
    const gfx::Sampler& sm = s.c.sampler;
    bool mipLevels = sm.mipAuto;
    for (uint32_t k = 0; k < 6; ++k) mipLevels = mipLevels || sm.mipBase[k] || sm.mipWidth[k];
    if (mipLevels) {
        const uint32_t mxl = std::min<uint32_t>(sm.mipMax, 6);
        for (uint32_t i = 1; i <= mxl; ++i) {
            const Level& prev = levels[i - 1];
            if (prev.w == 1 && prev.h == 1) break;
            Level& l = levels[i];
            l.w = std::max<uint32_t>(prev.w / 2, 1);
            l.h = std::max<uint32_t>(prev.h / 2, 1);
            if (sm.mipAuto && i <= 3) {   // MTBA
                l.tbp = prev.tbp + prev.w * prev.h * psmBits(t.psm) / 8 / 256;
                l.tbw = std::max<uint32_t>(prev.tbw / 2, 1);
            } else {
                l.tbp = sm.mipBase[i - 1];
                l.tbw = sm.mipWidth[i - 1];
            }
            const uint32_t lrows = (l.h + ph - 1) / ph, lperRow = std::max<uint32_t>(1, (t.psm == PSMT8 || t.psm == PSMT4) ? l.tbw / 2 : l.tbw);
            for (uint32_t py = 0; py < lrows; ++py)
                for (uint32_t px = 0; px < std::max<uint32_t>(1, std::min<uint32_t>(lperRow, (l.w + pw - 1) / pw)); ++px)
                {
                    const uint32_t page = (l.tbp / 32 + py * lperRow + px) & 511;
                    generation += pageGeneration[page] * (i * 512 + py * lperRow + px + 1);
                    content = mix(content, page);
                }
            levelCount = i + 1;
        }
    }
    // GS memory, decoded once per content: the texture's fields, TEXA for 16/24-bit, the palette in use.
    uint64_t key = mix(mix(mix(0, t.tbp), t.tbw), t.psm);
    key = mix(mix(key, t.tw), t.th);
    for (uint32_t i = 1; i < levelCount; ++i) key = mix(mix(key, levels[i].tbp), levels[i].tbw);
    uint64_t clutHash = 0;
    if (indexed) {
        clutHash = mix(mix(0, t.cpsm), t.csa);
        const uint8_t* clut = mem.clutBuffer();
        for (uint32_t k = 0; k < 1024; k += 8) {
            uint64_t v;
            std::memcpy(&v, clut + k, 8);
            clutHash = mix(clutHash, v);
        }
        key = mix(key, clutHash);   // one texture per palette: GT4 draws one index image with several
    } else if (t.psm != PSMCT32) {
        key = mix(key, uint64_t(s.texAlpha0) | (uint64_t(s.texBlackTransparent) << 15) | (uint64_t(s.texAlpha1) << 32));
    }
    // Garbage TEX0 registers decode a texture per draw; a frame keeps to a sane number (and GPU memory).
    constexpr size_t kMaxDecodesPerFrame = 768;
    if (frame.textures.size() >= kMaxDecodesPerFrame && !textureCache.count(key)) return TextureSource{};
    Cached& c = textureCache[key];
    c.lastUsed = frameCount;
    // A texture pack's image for this content (texture_name.h names it as PCSX2 does).
    std::shared_ptr<const ReplacementImage> replacement;
    // The replacement a texture is drawn with stays uploaded while it is used: each use keeps it. One dropped
    // meanwhile (unused for two seconds by every texture it replaced) is named and fetched again; until then
    // its id names no texture, and the draws using it came out empty (the arcade car selection's emblems and
    // car body, still for longer than that).
    bool lost = false;
    if (c.replacedId) {
        auto kept = replacementTextures.find(c.replacementName);
        if (kept != replacementTextures.end() && kept->second.id == c.replacedId) {
            kept->second.lastUsed = frameCount;
        } else {
            lost = true;
            c.replacedId = 0;
        }
    }
    // (a decoded texture dropped meanwhile: decoded again)
    auto shown = c.id ? decoded.find(c.contentKey) : decoded.end();
    const bool gone = c.id && !c.replacedId && (shown == decoded.end() || shown->second.id != c.id);
    const bool fresh = !c.version || c.generation != generation || c.clutHash != clutHash || lost || gone;
    if (!fresh && shown != decoded.end()) shown->second.lastUsed = frameCount;
    // A stream: new content at this address and size on three or more consecutive frames (the menus' movie
    // backgrounds, a video frame uploaded into the frame buffer each frame) is decoded into the texture the last
    // one had, not a new texture (a GPU image, a texture-pack lookup) that the cache kept for 30 seconds (one per
    // frame: the budget's evictions and the GPU's image churn in every menu). Not twice in one frame: the frame's
    // earlier draws keep the pixels they were given.
    const uint64_t previousKey = c.contentKey, previousDecode = c.decodedFrame;
    const uint32_t previousId = c.id;
    const bool consecutive = previousDecode != ~0ull && previousDecode + 2 >= frameCount && previousDecode != frameCount;
    const bool streaming = fresh && previousId && !c.replacedId && consecutive && c.streak >= 2;
    // What the texture is (when it may have changed): its fields and palette (key), the content of every page its
    // levels cover.
    uint64_t contentKey = c.contentKey;
    if (fresh) {
        contentKey = mix(key, content);
        for (uint32_t i = 0; i < levelCount; ++i) {
            const Level& l = levels[i];
            const uint32_t lrows = (l.h + ph - 1) / ph, lperRow = std::max<uint32_t>(1, (t.psm == PSMT8 || t.psm == PSMT4) ? l.tbw / 2 : l.tbw);
            for (uint32_t py = 0; py < lrows; ++py)
                for (uint32_t px = 0; px < std::max<uint32_t>(1, std::min<uint32_t>(lperRow, (l.w + pw - 1) / pw)); ++px)
                    for (uint32_t extra = 0; extra < 2; ++extra) {
                        if (extra && !(l.tbp % 32)) break;
                        contentKey = mix(contentKey, pageContent(l.tbp / 32 + py * lperRow + px + extra));
                    }
        }
        c.contentKey = contentKey;
    }
    if (replacementsOn) {
        if (fresh) {   // named once per content (level 0 is replaced; the GPU makes a mipmapped texture's levels)
            Decoded& known = decoded[contentKey];
            known.lastUsed = frameCount;
            if (!known.named && streaming) known.named = true;   // (a stream's frames: no pack has them)
            if (!known.named) {
                MipAddress mips[6];
                for (uint32_t i = 1; i < levelCount; ++i) mips[i - 1] = {levels[i].tbp, levels[i].tbw};
                GT4_CPU_SCOPE("front end: texture pack naming");
                known.found = findTextureName(mem, t, Texa{s.texAlpha0, s.texAlpha1, s.texBlackTransparent != 0}, sm, mips, levelCount - 1,
                                              *texturePack, known.name, known.origin);
                known.named = true;
                ++stats.named;
                stats.found += known.found;
            }
            c.replacementFound = known.found;
            c.replacementName = known.name;
            c.replacementOrigin[0] = known.origin[0];
            c.replacementOrigin[1] = known.origin[1];
            // GT4_TEXPACK_LOG=N (debugging): the first N textures looked up, found or not.
            static long logLeft = std::getenv("GT4_TEXPACK_LOG") ? std::strtol(std::getenv("GT4_TEXPACK_LOG"), nullptr, 10) : 0;
            if (logLeft > 0) {
                --logLeft;
                std::fprintf(stderr, "texpack: tbp %04x psm %02x %ux%u clamp %u/%u mips %u -> %s %016llx-%016llx-%08x\n", t.tbp, t.psm, w, h,
                             sm.wrapU, sm.wrapV, levelCount - 1, c.replacementFound ? "found" : "none",
                             static_cast<unsigned long long>(c.replacementName.texHash),
                             static_cast<unsigned long long>(c.replacementName.clutHash), c.replacementName.bits);
            }
            c.replacementPending = false;
        }
        const uint64_t loads = texturePack->loadedSerial();
        if (c.replacementFound && (fresh || (c.replacementPending && loads != c.packLoads))) {
            c.packLoads = loads;
            replacement = texturePack->get(c.replacementName);
            c.replacementPending = !replacement;
        }
    }
    if (replacement) {   // the image's own texture, uploaded once while it is in use (GT4 cycles texture slots)
        c.generation = generation;
        c.clutHash = clutHash;
        if (!c.version) c.version = 1;
        c.replacedId = replacementTexture(c.replacementName, *replacement);
    } else if (fresh) {
        c.replacedId = 0;
        c.generation = generation;
        c.clutHash = clutHash;
        if (!c.version) c.version = 1;
        Decoded& d = decoded[contentKey];
        d.lastUsed = frameCount;
        if (d.id) {   // seen before: its texture is still on the GPU
            c.id = d.id;
            c.streak = 0;
            ++stats.reused;
            src.kind = TextureSource::Kind::Memory;
            src.id = c.id;
            src.version = 1;
            return src;
        }
        uint32_t id = 0;
        if (streaming) {   // (the previous content's texture, unless a draw of this frame shows it)
            auto old = decoded.find(previousKey);
            if (old != decoded.end() && old->second.id == previousId && !old->second.replacedId && old->second.lastUsed != frameCount) {
                cachedTexels -= old->second.texels;
                decoded.erase(old);
                id = previousId;
                ++stats.streamed;
            }
        }
        c.streak = consecutive ? c.streak + 1 : 1;
        c.decodedFrame = frameCount;
        d.id = c.id = id ? id : nextTexture++;
        if (!id) setTextureIdentity(d.id, contentKey);   // (B6; a stream's reused id keeps its first content's: one material)
        d.texels = 0;
        for (uint32_t i = 0; i < levelCount; ++i) d.texels += uint64_t(levels[i].w) * levels[i].h;
        cachedTexels += d.texels;
        TextureImage image;
        image.id = c.id;
        image.version = 1;
        image.width = w;
        image.height = h;
        image.levels = levelCount;
        size_t texels = 0;
        for (uint32_t i = 0; i < levelCount; ++i) texels += size_t(levels[i].w) * levels[i].h;
        image.pixels.resize(texels);
        Texa texa;
        texa.ta0 = s.texAlpha0;
        texa.aem = s.texBlackTransparent;
        texa.ta1 = s.texAlpha1;
        size_t at = 0;
        // A stream's frame wider than its buffer (a 640-wide video frame declared 1024 texels wide): only the
        // buffer's width is decoded, its last column repeated to the right.
        const uint32_t shownWidth = std::max<uint32_t>(t.tbw, 1) * 64;
        const bool part = id && levelCount == 1 && !indexed && shownWidth < w;
        if (part) {
            std::vector<uint32_t> visible(size_t(shownWidth) * h);
            mem.decode(t, texa, 0, 0, shownWidth, h, visible.data());
            for (uint32_t y = 0; y < h; ++y) {
                uint32_t* row = image.pixels.data() + size_t(y) * w;
                std::memcpy(row, visible.data() + size_t(y) * shownWidth, size_t(shownWidth) * 4);
                std::fill(row + shownWidth, row + w, row[shownWidth - 1]);
            }
        }
        for (uint32_t i = 0; i < (part ? 0u : levelCount); ++i) {
            GsMemory::Tex0 lt = t;
            lt.tbp = levels[i].tbp;
            lt.tbw = levels[i].tbw;
            mem.decode(lt, texa, 0, 0, levels[i].w, levels[i].h, image.pixels.data() + at);
            at += size_t(levels[i].w) * levels[i].h;
        }
        // GT4_GSN_TEXDUMP=TBP (hex, debugging): decoded textures at that address, as gsn_tex_TBP_N.ppm.
        static const long dumpTbp = std::getenv("GT4_GSN_TEXDUMP") ? std::strtol(std::getenv("GT4_GSN_TEXDUMP"), nullptr, 16) : -1;
        if (static_cast<long>(t.tbp) == dumpTbp) {
            static int dumped = 0;
            const std::string name = "gsn_tex_" + std::to_string(t.tbp) + "_" + std::to_string(dumped++) + ".ppm";
            if (FILE* f = std::fopen(name.c_str(), "wb")) {
                std::fprintf(f, "P6\n%u %u\n255\n", w, h);
                for (size_t k = 0; k < size_t(w) * h; ++k) {
                    const uint32_t p = image.pixels[k];
                    const uint8_t rgb[3] = {static_cast<uint8_t>(p), static_cast<uint8_t>(p >> 8), static_cast<uint8_t>(p >> 16)};
                    std::fwrite(rgb, 1, 3, f);
                }
                std::fclose(f);
            }
            if (FILE* f = std::fopen((name.substr(0, name.size() - 4) + "_a.pgm").c_str(), "wb")) {   // alpha
                std::fprintf(f, "P5\n%u %u\n255\n", w, h);
                for (size_t k = 0; k < size_t(w) * h; ++k) std::fputc(static_cast<int>(image.pixels[k] >> 24), f);
                std::fclose(f);
            }
        }
        summarize(textureIdentity(c.id), image);   // (F2b; a stream's later contents keep the first's, as its identity does)
        frame.textures.push_back(std::move(image));
        ++stats.decoded;
    }
    if (c.replacedId) {
        src.kind = TextureSource::Kind::Memory;
        src.id = c.replacedId;
        src.version = 1;
        src.regionX = static_cast<uint16_t>(c.replacementOrigin[0]);
        src.regionY = static_cast<uint16_t>(c.replacementOrigin[1]);
        src.regionWidth = static_cast<uint16_t>(c.replacementName.regionWidth);
        src.regionHeight = static_cast<uint16_t>(c.replacementName.regionHeight);
        return src;
    }
    src.kind = TextureSource::Kind::Memory;
    src.id = c.id;
    src.version = 1;
    return src;
}

void GsFrontend::packed(uint32_t r, uint64_t a, uint64_t b) {
    switch (r) {
    case PRIM: write(PRIM, a & 0x7FF); break;
    case RGBAQ: {
        const uint64_t rgba = (a & 0xFF) | (((a >> 32) & 0xFF) << 8) | ((b & 0xFF) << 16) | (((b >> 32) & 0xFF) << 24);
        write(RGBAQ, rgba | (static_cast<uint64_t>(std::bit_cast<uint32_t>(q)) << 32));
        break;
    }
    case ST:
        write(ST, a);
        q = asFloat(static_cast<uint32_t>(b));
        break;
    case UV: write(UV, (a & 0x3FFF) | (((a >> 32) & 0x3FFF) << 16)); break;
    case XYZF2: case XYZF3: {
        const uint64_t v = (a & 0xFFFF) | (((a >> 32) & 0xFFFF) << 16) | (((b >> 4) & 0xFFFFFF) << 32) | (((b >> 36) & 0xFF) << 56);
        const bool adc = (b >> 47) & 1;
        write(r == XYZF3 || adc ? XYZF3 : XYZF2, v);
        break;
    }
    case XYZ2: case XYZ3: {
        const uint64_t v = (a & 0xFFFF) | (((a >> 32) & 0xFFFF) << 16) | ((b & 0xFFFFFFFF) << 32);
        const bool adc = (b >> 47) & 1;
        write(r == XYZ3 || adc ? XYZ3 : XYZ2, v);
        break;
    }
    case FOG: write(FOG, ((b >> 36) & 0xFF) << 56); break;
    case AD: write(static_cast<uint32_t>(b & 0x7F), a); break;
    case NOP: break;
    default: write(r, a); break;   // TEX0, CLAMP, ...: the low 64 bits are the register
    }
}

void GsFrontend::setPrim(uint32_t prim) { write(PRIM, prim & 0x7FF); }

void GsFrontend::effect(const gfx::EffectRecord& r) {
    if (r.kind == gfx::kSkipBegin || r.kind == gfx::kSkipEnd) {
        skipping = r.kind == gfx::kSkipBegin && nativeEffects;
        return;
    }
    if (r.kind != gfx::kGrade || !nativeEffects) return;
    FrameList::Effect e;
    e.kind = r.kind;
    e.x0 = static_cast<uint32_t>(r.scissor & 0x7FF);
    e.x1 = static_cast<uint32_t>((r.scissor >> 16) & 0x7FF);
    e.y0 = static_cast<uint32_t>((r.scissor >> 32) & 0x7FF);
    e.y1 = static_cast<uint32_t>((r.scissor >> 48) & 0x7FF);
    if (e.x0 > e.x1 || e.y0 > e.y1) return;
    // the frames as the draws know them (a 24-bit frame: the 32-bit layout)
    e.source = targetFor(r.sourcePage * 32u, PSMCT32, false, e.y1 + 1, r.width);
    e.destination = targetFor(r.destinationPage * 32u, r.destinationFormat, false, e.y1 + 1, r.width);
    if (!e.source || !e.destination) return;
    for (int k = 0; k < 3; ++k) e.weights[k] = r.weights[k];
    // the curve: an 8-bit texture's palette at clutBase, 32-bit, as the GS loads it
    GsMemory::Tex0 t;
    t.psm = PSMT8;
    t.cpsm = PSMCT32;
    t.cbp = r.clutBase;
    materializeRect(r.clutBase, 1, PSMCT32, 0, 0, 16, 16, kForTexture);
    mem.readClut(t, e.curve.data());
    sceneRead(e.source);   // (a 3D scene read into another: complete)
    markWritten(e.destination, r.destinationPage * 32u, r.width, r.destinationFormat, e.x0, e.x1, e.y0, e.y1);
    frame.commands.push_back({FrameList::Command::Kind::Effect, static_cast<uint32_t>(frame.effects.size())});
    frame.effects.push_back(e);
}

// R10 spike (local): per frame, how many batches carry each value of qwords 201..216 (the lighting constants)
static std::map<uint64_t, uint32_t> r10Constants;
static std::vector<float> r10Lanes[64];   // per frame: each float of qwords 201..216, one entry per batch
static uint64_t r10LastVb = 0;            // the VBlank of the last frame finished
// GT4_R10_FREEZE=FROM,TO,QLO,QHI (ablation): each model job (keyed by its renderer, chain, counts, strip header and
// its place among jobs with the same key) gets for qwords QLO..QHI the values it had in the last frame before FROM,
// in the frames after FROM up to TO. Everything else (geometry, matrices) stays as sent.
struct R10Freeze {
    bool on = false;
    unsigned long long from = 0, to = 0;
    unsigned qlo = 0, qhi = 0;
    uint64_t frameOf = ~0ull;
    std::map<uint64_t, unsigned> seen;
    std::map<uint64_t, std::vector<uint32_t>> saved;
    unsigned long long frozen = 0, missed = 0;
    R10Freeze() {
        const char* e = std::getenv("GT4_R10_FREEZE");
        on = e && std::sscanf(e, "%llu,%llu,%u,%u", &from, &to, &qlo, &qhi) == 4 && qhi < 256 && qlo <= qhi;
    }
};
static R10Freeze r10Freeze;

void GsFrontend::model(const gfx::ModelJob& in, const uint32_t* words) {
    GT4_CPU_SCOPE("front end: 3D batches");
    static std::vector<uint32_t> r10Copy(gfx::kModelWords);
    if (r10Freeze.on) {
        R10Freeze& F = r10Freeze;
        if (F.frameOf != r10LastVb) {
            F.frameOf = r10LastVb;
            F.seen.clear();
            if (r10LastVb == F.to) std::fprintf(stderr, "R10 freeze: %llu jobs frozen, %llu without a saved key\n", F.frozen, F.missed);
        }
        uint64_t h = 1469598103934665603ull;
        auto mix = [&](uint32_t v) { h = (h ^ v) * 1099511628211ull; };
        mix(in.renderer), mix(in.chain), mix(in.count), mix(in.nloop);
        const uint32_t strips = std::min<uint32_t>(words[64 * 4 + 3] & 0xFFFF, 60);
        for (uint32_t k = 0; k <= strips; ++k) mix(words[(64 + k) * 4 + 3]);
        const uint64_t key = h ^ (uint64_t(F.seen[h]++) << 48);
        const uint32_t lo = F.qlo * 4, n = (F.qhi - F.qlo + 1) * 4;
        if (r10LastVb < F.from) {
            F.saved[key].assign(words + lo, words + lo + n);
        } else if (r10LastVb <= F.to) {
            auto it = F.saved.find(key);
            if (it != F.saved.end()) {
                std::copy(words, words + gfx::kModelWords, r10Copy.begin());
                std::copy(it->second.begin(), it->second.end(), r10Copy.begin() + lo);
                words = r10Copy.data();
                ++F.frozen;
            } else {
                ++F.missed;
            }
        }
    }
    {
        uint64_t h = 1469598103934665603ull;
        for (uint32_t i = 201 * 4; i < 217 * 4; ++i) h = (h ^ words[i]) * 1099511628211ull;
        ++r10Constants[h ^ (uint64_t(in.renderer) << 60)];
        for (uint32_t i = 0; i < 64; ++i) {
            float f;
            std::memcpy(&f, &words[201 * 4 + i], 4);
            if (std::isfinite(f)) r10Lanes[i].push_back(f);
        }
    }
    // R10 ablation (local): GT4_R10_SKIPBODY=hex drops the model jobs with that lighting body (chain >> 16),
    // GT4_R10_SKIPTEX=id the ones that sample that texture
    static const long r10SkipBody = [] { const char* e = std::getenv("GT4_R10_SKIPBODY"); return e ? std::strtol(e, nullptr, 16) : -1L; }();
    static const std::vector<long> r10SkipTex = [] {   // a comma list
        std::vector<long> v;
        if (const char* e = std::getenv("GT4_R10_SKIPTEX"))
            for (char* p = const_cast<char*>(e); *p;) {
                char* end = nullptr;
                const long id = std::strtol(p, &end, 10);
                if (end == p) break;
                v.push_back(id);
                p = *end ? end + 1 : end;
            }
        return v;
    }();
    if (r10SkipBody >= 0 && long(in.chain >> 16) == r10SkipBody) return;
    setPrim(in.prim);
    const uint32_t prim = primValue();
    queued = 0;   // (the next batch starts its strips again: its first two vertices complete no triangle)
    // Its triangles: per strip, one per vertex from the third on, of the first `count` vertices.
    uint32_t triangles = 0;
    {
        const uint32_t strips = words[64 * 4 + 3] & 0xFFFF;
        uint32_t v = 0;
        for (uint32_t k = 0; k < strips && v < in.count; ++k) {
            const uint32_t n = (words[(65 + k) * 4 + 3] & 0xFFFF) / 3u + 2;
            const uint32_t drawn = std::min(n, in.count - v);
            if (drawn > 2) triangles += drawn - 2;
            v += n;
        }
    }
    if (!triangles) return;
    const DrawState s = currentState(prim);
    const gfx::Context& c = contexts[(prim & gfx::kContext2) ? 1 : 0];
    const uint32_t jitter = originMask();
    const uint32_t scissorX0 = s.c.scissorX0, scissorX1 = s.c.scissorX1, scissorY0 = s.c.scissorY0, scissorY1 = s.c.scissorY1;
    const uint32_t width = s.c.colorWidth;
    const uint32_t colour = targetFor(s.c.colorBase, s.c.colorFormat, false, scissorY1 + 1, width);
    const bool zte = s.c.depthTest && !(s.c.depthFunc == 1 && !s.c.depthWrite);
    const uint32_t depth = zte ? targetFor(s.c.depthBase, s.c.depthFormat, true, scissorY1 + 1, width) : 0;
    if (!colour || (zte && !depth)) return;   // past the target limit
    // what it writes: its scissor (the vertices are the GPU's); again only when something else wrote since
    const std::array<uint32_t, 8> marking = {colour, s.c.colorKeep != 0xFFFFFFFFu, depth, depth && s.c.depthWrite, scissorX0, scissorX1, scissorY0, scissorY1};
    if (marking != lastModelMarking || serial != lastModelSerial) {
        if (s.c.colorKeep != 0xFFFFFFFFu) markWritten(colour, s.c.colorBase, width, s.c.colorFormat, scissorX0, scissorX1, scissorY0, scissorY1);
        if (depth && s.c.depthWrite) markWritten(depth, s.c.depthBase, width, s.c.depthFormat, scissorX0, scissorX1, scissorY0, scissorY1);
        lastModelMarking = marking;
        lastModelSerial = serial;
    }
    TextureSource texture;
    if ((prim >> 4) & 1) {
        const LastTexture& m = lastTexture;
        if (m.valid && m.uploads == uploads && m.ownerChanges == ownerChanges && m.clutLoads == clutLoads && m.state == s) {
            texture = m.source;
        } else {
            texture = textureFor(s);
            lastTexture = {texture.kind == TextureSource::Kind::Memory, s, texture, uploads, ownerChanges, clutLoads};
        }
    }
    if (!r10SkipTex.empty() && texture.kind == TextureSource::Kind::Memory &&
        std::find(r10SkipTex.begin(), r10SkipTex.end(), long(texture.id)) != r10SkipTex.end()) return;
    if (!n3SkipIdentities.empty() && texture.kind == TextureSource::Kind::Memory &&
        n3SkipIdentities.count(textureIdentity(texture.id))) return;   // LOCAL N3 DEBUG: v4's textures (GT4_N3_V4SKIP)
    {   // LOCAL N3 DEBUG: GT4_N3_SKIPIDS=hex,hex,... skips the draws of exactly those texture identities (a bisection)
        static const std::unordered_set<uint64_t> skipIds = [] {
            std::unordered_set<uint64_t> s;
            if (const char* e = std::getenv("GT4_N3_SKIPIDS"))
                for (const char* p = e; *p;) {
                    char* end = nullptr;
                    const unsigned long long v = std::strtoull(p, &end, 16);
                    if (end == p) break;
                    s.insert(v);
                    p = *end == ',' ? end + 1 : end;
                }
            return s;
        }();
        if (!skipIds.empty() && texture.kind == TextureSource::Kind::Memory && skipIds.count(textureIdentity(texture.id))) return;
    }
    if (texture.kind == TextureSource::Kind::Target && texture.id != colour) sceneRead(texture.id);
    noteDraw(colour, depth, texture);
    const bool moving = (in.flags & 4) != 0;
    // its scene view (CONTRACTS.md a.5 7, a.8: the stream's decoder put its instance in pad[0], that instance's view in
    // pad[1]): the view is bound to the colour target of its first tagged draw, and counted split if another follows
    const uint16_t tagged = static_cast<uint16_t>(in.pad[0] ? in.pad[1] : 0);
    // A6: a car light map's draw (kInstLightMap, gfx::kJobLightMap in pad[1]) and the road reflection's
    // (kInstReflection, gfx::kJobReflection) are out of the binding (a.5.7): render-to-texture, treated as untagged and
    // counted with the targets they write
    const bool lightMap = tagged && (in.pad[1] & gfx::kJobLightMap) && sceneOpen;
    const bool reflection = tagged && (in.pad[1] & gfx::kJobReflection) && sceneOpen;
    if (lightMap || reflection) {
        ++(lightMap ? stats.viewLightMapDraws : stats.viewReflectionDraws);
        std::vector<uint32_t>& seen = lightMap ? stats.lightMapTargets : stats.reflectionTargets;
        if (std::find(seen.begin(), seen.end(), colour) == seen.end() && seen.size() < 64) seen.push_back(colour);
        std::vector<uint32_t>& frameSeen = reflectionFrameTargets;
        if (reflection && std::find(frameSeen.begin(), frameSeen.end(), colour) == frameSeen.end()) {
            frameSeen.push_back(colour);
            stats.reflectionTargetsInFrame = std::max<uint64_t>(stats.reflectionTargetsInFrame, frameSeen.size());
        }
    }
    // a draw sampling a target the reflection wrote in this scene frame (its readers, by whether the draw is tagged:
    // a.7's viewReflectionDraws). By the frame's own targets: target ids are reused for other surfaces later
    const std::vector<uint32_t>& written = reflectionFrameTargets;
    if (texture.kind == TextureSource::Kind::Target && !written.empty() &&
        std::find(written.begin(), written.end(), texture.id) != written.end())
        ++stats.reflectionSamples[tagged && !lightMap && !reflection ? 1 : 0];
    const uint16_t view = lightMap || reflection ? 0 : tagged;
    if (view > kViews && sceneOpen) ++stats.viewsPastLimit;   // (not bound: counted apart from viewsUnbound)
    if (view && view <= kViews && sceneOpen) {
        if (viewBindings.size() < view) viewBindings.resize(view);
        ViewBinding& b = viewBindings[view - 1u];
        bindDraw(b, colour, depth, s.c.depthTest && s.c.depthFunc != 1, depth && s.c.depthWrite, texture);
        b.inList = true;
    }
    const uint16_t drawView = sceneSplit ? view : 0;
    uint8_t bracketRule = 0;
    const uint8_t bracket = bracketFor(colour, bracketRule);
    if (bracket == kBracketFlare && bracketRule && glowOrigins.size() < 4096)   // (A7's check: a glow onto the picture)
        glowOrigins.push_back({in.matrix[3][0], in.matrix[3][1], in.matrix[3][3]});
    Draw* batch = nullptr;
    if (!frame.commands.empty() && frame.commands.back().kind == FrameList::Command::Kind::Draw) {
        Draw& last = frame.draws[frame.commands.back().index];
        if (last.modeled && last.topology == Topology::Triangles && last.state == s && last.target == colour && last.depth == depth &&
            last.texture.kind == texture.kind && last.texture.id == texture.id && last.texture.version == texture.version &&
            last.bracket == bracket && last.first + last.count == frame.modelVertices) {
            // a view change inside a run that merges: the split makes it a new draw (split off: what it would add)
            if (view != lastModelView) ++stats.sceneSplitDraws;
            if (last.view == drawView) batch = &last;
        }
    }
    lastModelView = view;
    if (!batch) {
        Draw d;
        d.state = s;
        d.topology = Topology::Triangles;
        d.first = frame.modelVertices;
        d.target = colour;
        d.depth = depth;
        d.texture = texture;
        d.modeled = true;
        d.view = drawView;
        d.bracket = bracket;
        countBracket(bracketRule);
        frame.commands.push_back({FrameList::Command::Kind::Draw, static_cast<uint32_t>(frame.draws.size())});
        frame.draws.push_back(d);
        ++stats.draws;
        batch = &frame.draws.back();
    }
    batch->moving = batch->moving || moving;
    gfx::ModelJob job = in;
    job.first = frame.modelVertices;
    job.data = static_cast<uint32_t>(frame.modelData.size());
    job.originX = static_cast<float>(c.originX & jitter) / 16.0f;
    job.originY = static_cast<float>(c.originY & jitter) / 16.0f;
    job.u = static_cast<float>(reg[UV] & 0x3FFF) / 16.0f;
    job.v = static_cast<float>((reg[UV] >> 16) & 0x3FFF) / 16.0f;
    job.fogRegister = static_cast<uint32_t>(reg[FOG] >> 56);
    if ((prim >> 3) & 1) job.flags |= 2;   // Gouraud
    frame.modelVertices += triangles * 3;
    batch->count += triangles * 3;
    frame.modelData.insert(frame.modelData.end(), words, words + gfx::kModelWords);
    frame.models.push_back(job);
    // B6: a tagged job (pad[0] its instance), chained to its instance's earlier ones in this list, with its draw
    if (sceneSplit && sceneOpen && in.pad[0]) {
        const uint32_t instance = in.pad[0];
        if (sceneJobFirst.size() <= instance) {
            sceneJobFirst.resize(instance + 1, UINT32_MAX);
            sceneJobLast.resize(instance + 1, UINT32_MAX);
        }
        const uint32_t at = static_cast<uint32_t>(sceneJobs.size());
        sceneJobs.push_back({static_cast<uint32_t>(frame.models.size() - 1), static_cast<uint32_t>(batch - frame.draws.data()), UINT32_MAX,
                             frame.models.back().first, triangles * 3});
        if (sceneJobLast[instance] == UINT32_MAX) sceneJobFirst[instance] = at;
        else sceneJobs[sceneJobLast[instance]].next = at;
        sceneJobLast[instance] = at;
    }
    stats.primitives += triangles;
    if (depth) {
        Scene& scene = scenes[colour];
        scene.draws += triangles;
        scene.depth = depth;
        scene.width = std::max<uint32_t>(scene.width, scissorX1 + 1);
        scene.height = std::max<uint32_t>(scene.height, scissorY1 + 1);
        if ((prim >> 5) & 1) {
            scene.fogColour = s.fogColor;
            scene.fogged = true;
        }
        const std::array<uint16_t, 4> rect = {static_cast<uint16_t>(scissorX0), static_cast<uint16_t>(scissorY0),
                                              static_cast<uint16_t>(scissorX1), static_cast<uint16_t>(scissorY1)};
        auto known = std::find_if(scene.viewports.begin(), scene.viewports.end(), [&](const auto& v) { return v.first == rect; });
        if (known != scene.viewports.end()) known->second += triangles;
        else if (scene.viewports.size() < 8) scene.viewports.push_back({rect, triangles});
    }
}

void GsFrontend::vertices(const InputVertex* in, size_t count, bool withFog) {
    const uint32_t prim = primValue();
    // Triangle strips, not UV texturing (FST): the renderers' runs. Others vertex by vertex.
    if ((prim & 7) != 4 || ((prim >> 8) & 1) || count < 3) {
        for (size_t k = 0; k < count; ++k) vertex(in[k], withFog);
        return;
    }
    const DrawState s = currentState(prim);
    const gfx::Context& c = contexts[(prim & gfx::kContext2) ? 1 : 0];
    const uint32_t jitter = originMask();
    const float ox = static_cast<float>(c.originX & jitter) / 16.0f, oy = static_cast<float>(c.originY & jitter) / 16.0f;
    const float u = static_cast<float>(reg[UV] & 0x3FFF) / 16.0f, vv = static_cast<float>((reg[UV] >> 16) & 0x3FFF) / 16.0f;
    const uint32_t fogRegister = static_cast<uint32_t>(reg[FOG] >> 56);
    auto convert = [&](const InputVertex& i) {
        Vertex v;
        v.x = static_cast<float>(i.x & 0xFFFF) / 16.0f - ox;
        v.y = static_cast<float>(i.y & 0xFFFF) / 16.0f - oy;
        v.z = withFog ? (i.z & 0xFFFFFF) : i.z;
        v.fog = withFog ? (i.fog & 0xFF) : fogRegister;
        v.rgba = i.rgba;
        v.s = i.s;
        v.t = i.t;
        v.q = i.q;
        v.u = u;
        v.v = vv;
        v.mw = i.motion[2];
        v.mx = i.motion[0] - ox * i.motion[2];
        v.my = i.motion[1] - oy * i.motion[2];
        v.w = i.h[3];
        if (v.w != 0.0f) {
            v.x = i.h[0] - ox * v.w;
            v.y = i.h[1] - oy * v.w;
            v.z = std::bit_cast<uint32_t>(i.h[2]);
        }
        return v;
    };
    // Targets, texture and the scene's bookkeeping: once.
    const uint32_t scissorX0 = s.c.scissorX0, scissorX1 = s.c.scissorX1, scissorY0 = s.c.scissorY0, scissorY1 = s.c.scissorY1;
    const uint32_t width = s.c.colorWidth;
    const uint32_t colour = targetFor(s.c.colorBase, s.c.colorFormat, false, scissorY1 + 1, width);
    const bool zte = s.c.depthTest && !(s.c.depthFunc == 1 && !s.c.depthWrite);
    const uint32_t depth = zte ? targetFor(s.c.depthBase, s.c.depthFormat, true, scissorY1 + 1, width) : 0;
    // The strip goes on from the last run's two vertices (the GS's queue), the registers end as the last vertex left them.
    Vertex ring[2] = {queue[0], queue[1]};
    uint32_t have = std::min<uint32_t>(queued, 2);
    if (!colour || (zte && !depth)) {   // past the target limit: nothing drawn, the queue kept
        for (size_t k = 0; k < count; ++k) vertex(in[k], withFog);
        return;
    }
    uint8_t bracketRule = 0;
    const uint8_t bracket = bracketFor(colour, bracketRule);
    TextureSource texture;
    bool first = true;
    Draw* batch = nullptr;
    uint32_t triangles = 0;
    const bool gouraud = (prim >> 3) & 1;
    for (size_t k = 0; k < count; ++k) {
        const Vertex v = convert(in[k]);
        const bool draws = have == 2 && !in[k].adc;
        if (draws) {
            Vertex t[3] = {ring[0], ring[1], v};
            if (!gouraud) t[0].rgba = t[1].rgba = v.rgba;
            // its extent (homogeneous vertices: the projected points; one behind the eye: the scissor's)
            float x0 = 1e30f, x1 = -1e30f, y0 = 1e30f, y1 = -1e30f;
            for (int j = 0; j < 3; ++j) {
                float x = t[j].x, y = t[j].y;
                if (t[j].w != 0.0f) {
                    if (!(t[j].w > 0.0f)) {
                        x0 = float(scissorX0); x1 = float(scissorX1 + 1); y0 = float(scissorY0); y1 = float(scissorY1 + 1);
                        break;
                    }
                    x /= t[j].w;
                    y /= t[j].w;
                }
                x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y);
            }
            const uint32_t left = static_cast<uint32_t>(std::clamp(x0, float(scissorX0), float(scissorX1)));
            const uint32_t right = static_cast<uint32_t>(std::clamp(std::max(x0, std::ceil(x1) - 1.0f), float(scissorX0), float(scissorX1)));
            const uint32_t top = static_cast<uint32_t>(std::clamp(y0, float(scissorY0), float(scissorY1)));
            const uint32_t bottom = static_cast<uint32_t>(std::clamp(std::max(y0, std::ceil(y1) - 1.0f), float(scissorY0), float(scissorY1)));
            if (s.c.colorKeep != 0xFFFFFFFFu) markWritten(colour, s.c.colorBase, width, s.c.colorFormat, left, right, top, bottom);
            if (depth && s.c.depthWrite) markWritten(depth, s.c.depthBase, width, s.c.depthFormat, left, right, top, bottom);
            if (first) {   // the texture as the first triangle sees it (as emit would, after its pages were marked)
                first = false;
                if ((prim >> 4) & 1) {
                    const LastTexture& m = lastTexture;
                    if (m.valid && m.uploads == uploads && m.ownerChanges == ownerChanges && m.clutLoads == clutLoads && m.state == s) {
                        texture = m.source;
                    } else {
                        texture = textureFor(s);
                        lastTexture = {texture.kind == TextureSource::Kind::Memory, s, texture, uploads, ownerChanges, clutLoads};
                    }
                }
                if (texture.kind == TextureSource::Kind::Target && texture.id != colour) sceneRead(texture.id);
                // a texture from memory stays for the run; another (a target's pixels) is looked up per triangle
                if (texture.kind != TextureSource::Kind::Memory && texture.kind != TextureSource::Kind::None) {
                    // (not the renderers' case: hand the rest of the run to the per-vertex path, from this vertex on)
                    queue[0] = ring[0];
                    queue[1] = ring[1];
                    queued = have;
                    for (size_t r = k; r < count; ++r) vertex(in[r], withFog);
                    return;
                }
            }
            if (!batch && !frame.commands.empty() && frame.commands.back().kind == FrameList::Command::Kind::Draw) {
                Draw& last = frame.draws[frame.commands.back().index];
                if (last.topology == Topology::Triangles && last.state == s && last.target == colour && last.depth == depth &&
                    last.texture.kind == texture.kind && last.texture.id == texture.id && last.texture.version == texture.version &&
                    last.bracket == bracket && last.first + last.count == frame.vertices.size())
                    batch = &last;
            }
            if (!batch) {
                Draw d;
                d.state = s;
                d.topology = Topology::Triangles;
                d.first = static_cast<uint32_t>(frame.vertices.size());
                d.count = 0;
                d.target = colour;
                d.depth = depth;
                d.texture = texture;
                d.bracket = bracket;
                countBracket(bracketRule);
                frame.commands.push_back({FrameList::Command::Kind::Draw, static_cast<uint32_t>(frame.draws.size())});
                frame.draws.push_back(d);
                ++stats.draws;
                batch = &frame.draws.back();
            }
            frame.vertices.insert(frame.vertices.end(), t, t + 3);
            batch->count += 3;
            ++triangles;
        }
        ring[0] = ring[1];
        ring[1] = v;
        have = std::min<uint32_t>(have + 1, 2);
    }
    stats.primitives += triangles;
    if (triangles && depth) {
        Scene& scene = scenes[colour];
        scene.draws += triangles;
        scene.depth = depth;
        scene.width = std::max<uint32_t>(scene.width, scissorX1 + 1);
        scene.height = std::max<uint32_t>(scene.height, scissorY1 + 1);
        if ((prim >> 5) & 1) {
            scene.fogColour = s.fogColor;
            scene.fogged = true;
        }
        const std::array<uint16_t, 4> rect = {static_cast<uint16_t>(scissorX0), static_cast<uint16_t>(scissorY0),
                                              static_cast<uint16_t>(scissorX1), static_cast<uint16_t>(scissorY1)};
        auto known = std::find_if(scene.viewports.begin(), scene.viewports.end(), [&](const auto& v) { return v.first == rect; });
        if (known != scene.viewports.end()) known->second += triangles;
        else if (scene.viewports.size() < 8) scene.viewports.push_back({rect, triangles});
    }
    // The GS's state after the run: the queue (a strip goes on), the registers the last vertex wrote.
    queue[0] = ring[0];
    queue[1] = ring[1];
    queued = have;
    const InputVertex& last = in[count - 1];
    st[0] = last.s;
    st[1] = last.t;
    q = last.q;
    reg[ST] = static_cast<uint64_t>(std::bit_cast<uint32_t>(last.s)) | static_cast<uint64_t>(std::bit_cast<uint32_t>(last.t)) << 32;
    reg[RGBAQ] = last.rgba | (static_cast<uint64_t>(std::bit_cast<uint32_t>(last.q)) << 32);
    motion[0] = motion[1] = motion[2] = 0.0f;
}

void GsFrontend::vertex(const InputVertex& v, bool withFog) {
    for (int i = 0; i < 3; ++i) motion[i] = v.motion[i];
    for (int i = 0; i < 4; ++i) homogeneous[i] = v.h[i];
    write(ST, static_cast<uint64_t>(std::bit_cast<uint32_t>(v.s)) | static_cast<uint64_t>(std::bit_cast<uint32_t>(v.t)) << 32);
    q = v.q;
    write(RGBAQ, v.rgba | (static_cast<uint64_t>(std::bit_cast<uint32_t>(v.q)) << 32));
    const uint64_t xy = (v.x & 0xFFFF) | static_cast<uint64_t>(v.y & 0xFFFF) << 16;
    if (withFog) write(v.adc ? XYZF3 : XYZF2, xy | static_cast<uint64_t>(v.z & 0xFFFFFF) << 32 | static_cast<uint64_t>(v.fog & 0xFF) << 56);
    else write(v.adc ? XYZ3 : XYZ2, xy | static_cast<uint64_t>(v.z) << 32);
}

void GsFrontend::gif(uint32_t path, const uint8_t* data, size_t bytes) {
    GifPath& g = gifPaths[path & 3];
    ++packetsThisFrame;
    size_t q16 = 0;
    const size_t total = bytes / 16;
    while (q16 < total) {
        uint64_t lo, hi;
        std::memcpy(&lo, data + q16 * 16, 8);
        std::memcpy(&hi, data + q16 * 16 + 8, 8);
        if (!g.loops) {   // a tag
            ++q16;
            g.loops = static_cast<uint32_t>(lo & 0x7FFF);
            g.flg = static_cast<uint32_t>(lo >> 58) & 3;
            g.nreg = static_cast<uint32_t>(lo >> 60) & 0xF;
            if (!g.nreg) g.nreg = 16;
            g.regs = hi;
            g.reg = 0;
            if (((lo >> 46) & 1) && g.flg == 0) write(PRIM, (lo >> 47) & 0x7FF);
            if (g.flg == 3) g.flg = 2;   // FLG 3 is IMAGE too
            continue;
        }
        if (g.flg == 0) {   // PACKED: one register per qword
            packed(static_cast<uint32_t>(g.regs >> (4 * g.reg)) & 0xF, lo, hi);
            ++q16;
            if (++g.reg == g.nreg) {
                g.reg = 0;
                --g.loops;
            }
        } else if (g.flg == 1) {   // REGLIST: two 64-bit register values per qword (the last one padded)
            for (const uint64_t v : {lo, hi}) {
                if (!g.loops) break;
                const uint32_t r = static_cast<uint32_t>(g.regs >> (4 * g.reg)) & 0xF;
                if (r != AD && r != NOP) write(r, v);
                if (++g.reg == g.nreg) {
                    g.reg = 0;
                    --g.loops;
                }
            }
            ++q16;
        } else {   // IMAGE
            const size_t n = std::min<size_t>(g.loops, total - q16);
            if (imageActive) imageWritten(data + q16 * 16, n * 16);
            static const bool logImages = std::getenv("GT4_GSN_XFER") != nullptr;
            if (logImages) std::printf("[gsn image] path %u: %zu of %u qwords%s\n", path, n, g.loops, imageActive ? "" : " (no transfer)");
            q16 += n;
            g.loops -= static_cast<uint32_t>(n);
        }
    }
}

// A texture pack image's texture: uploaded with the first draw that uses it, then shared by every texture slot
// whose content it replaces until unused for two seconds (vsync).
uint32_t GsFrontend::replacementTexture(const TextureName& name, const ReplacementImage& image) {
    auto it = replacementTextures.find(name);
    if (it != replacementTextures.end()) {
        it->second.lastUsed = frameCount;
        return it->second.id;
    }
    TextureImage upload;
    upload.id = nextTexture++;
    setTextureIdentity(upload.id, TextureNameHash{}(name) ^ 0x7E3A9C0000000000ull);   // (B6: a pack image, by its name)
    upload.version = 1;
    upload.width = image.width;
    upload.height = image.height;
    upload.replaced = true;
    upload.pixels = image.pixels;
    frame.textures.push_back(std::move(upload));
    const uint64_t texels = uint64_t(image.width) * image.height;
    replacementTextures[name] = {upload.id, frameCount, texels};
    cachedTexels += texels;
    ++stats.replaced;
    return upload.id;
}

// F2b: a texture's summary by its identity, once (scene/texture_summary.h: at most 128 x 128 samples of level 0), timed
// into the front end's stats. Not for a pack's image: the verdict follows the game's texture, never the pack.
void GsFrontend::summarize(uint64_t identity, const TextureImage& image) {
    if (!identity || image.replaced || textureSummaries.count(identity)) return;
    const auto started = std::chrono::steady_clock::now();
    textureSummaries.emplace(identity, scene::summarizeTexture(image.pixels.data(), image.width, image.height));
    ++stats.textureSummaries;
    stats.textureSummaryMicroseconds += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - started).count();
}

// A texture id's identity (B6), counted per identity: two live ids can share one (the same set loaded into two slots).
void GsFrontend::setTextureIdentity(uint32_t id, uint64_t identity) {
    auto [it, fresh] = textureIdentities.try_emplace(id, identity);
    if (!fresh) {
        if (it->second == identity) return;
        retireTextureIdentity(id);
        it = textureIdentities.emplace(id, identity).first;
    }
    ++identityUses[identity];
}

// A texture id retired: its identity goes, and the identity's summary (F2b) with the last id that has it.
void GsFrontend::retireTextureIdentity(uint32_t id) {
    auto it = textureIdentities.find(id);
    if (it == textureIdentities.end()) return;
    auto uses = identityUses.find(it->second);
    if (uses == identityUses.end() || --uses->second == 0) {
        if (uses != identityUses.end()) identityUses.erase(uses);
        textureSummaries.erase(it->second);
    }
    textureIdentities.erase(it);
}

uint64_t GsFrontend::textureIdentity(uint32_t id) const {
    auto it = textureIdentities.find(id);
    return it == textureIdentities.end() ? 0 : it->second;
}

void GsFrontend::resetTextures() {
    lastTexture.valid = false;
    assetLoaded = 0;
    setPalettes.clear();
    replacementTextures.clear();
    textureCache.clear();
    decoded.clear();
    textureIdentities.clear();
    identityUses.clear();
    textureSummaries.clear();
    cachedTexels = 0;
    frame.textures.clear();
    frame.resetTextures = true;
}

// Two targets' GS memory overlaps (the same target included): their block ranges (FBP in pages of 32 blocks; 32-bit
// formats 32 rows a page, 16-bit ones 64; the height drawn so far), wrapping at 4 MiB (16384 blocks).
bool GsFrontend::overlaps(uint32_t a, uint32_t b) const {
    if (a == b) return a != 0;
    const Target *ta = nullptr, *tb = nullptr;
    for (const Target& t : targets) {
        if (t.id == a) ta = &t;
        if (t.id == b) tb = &t;
    }
    if (!ta || !tb) return false;
    static constexpr uint32_t kBlocks = 16384;   // (static: std::min takes it by reference, GCC wants it captured otherwise)
    auto span = [](const Target& t) {
        const bool sixteen = (t.psm & 0x0F) >= 2;   // PSMCT16/16S, PSMZ16/16S
        const uint32_t rows = sixteen ? 64u : 32u;
        const uint32_t pages = std::max<uint32_t>(t.bw, 1u) * ((std::max<uint32_t>(t.height, 1u) + rows - 1) / rows);
        return std::make_pair(t.bp % kBlocks, std::min<uint32_t>(pages * 32u, kBlocks));
    };
    const auto [a0, an] = span(*ta);
    const auto [b0, bn] = span(*tb);
    auto inside = [&](uint32_t x0, uint32_t xn, uint32_t y0, uint32_t yn) {   // [x0, x0 + xn) meets [y0, y0 + yn), mod kBlocks
        for (int wx = 0; wx < 2; ++wx)
            for (int wy = 0; wy < 2; ++wy) {
                const uint64_t xs = x0 + uint64_t(wx) * kBlocks, ys = y0 + uint64_t(wy) * kBlocks;
                if (xs < ys + yn && ys < xs + xn) return true;
            }
        return false;
    };
    return inside(a0, an, b0, bn);
}

// One tagged draw of a view (CONTRACTS.md a.5.7): summarised per colour target, decided at ViewEnd (finishBinding). A
// sample remembers the view's target overlapping it that was written before it (an auxiliary read, once that target
// turns out auxiliary), counted in draws.
void GsFrontend::bindDraw(ViewBinding& b, uint32_t colour, uint32_t depth, bool depthTested, bool writesDepth, const TextureSource& texture) {
    b.bound = true;
    const uint32_t seq = ++b.seq;
    TargetUse* u = nullptr;
    for (uint32_t i = 0; i < b.useCount; ++i)
        if (b.uses[i].colour == colour) u = &b.uses[i];
    // its sample, against what the view wrote before this draw
    uint32_t writtenBefore = 0, writtenSeq = 0;   // (the latest first-written of the targets overlapping it)
    if (texture.kind == TextureSource::Kind::Target)
        for (uint32_t i = 0; i < b.useCount; ++i)
            if (b.uses[i].colour != colour && b.uses[i].firstSeq > writtenSeq && overlaps(b.uses[i].colour, texture.id)) {
                writtenBefore = b.uses[i].colour;
                writtenSeq = b.uses[i].firstSeq;
            }
    if (!u) {
        if (b.useCount == kTargets) {
            b.overflow = true;
            return;
        }
        u = &b.uses[b.useCount++];
        *u = TargetUse{};
        u->colour = colour;
        u->firstSeq = seq;
    }
    ++u->draws;
    if (depthTested && !u->tested) {
        u->tested = true;
        u->testedSeq = seq;
    }
    if (depth && (depthTested || writesDepth)) {
        uint32_t k = 0;
        while (k < u->depthCount && u->depths[k].target != depth) ++k;
        if (k == u->depthCount) {
            if (k == 2) {
                b.overflow = true;
            } else {
                u->depths[u->depthCount++].target = depth;
            }
        }
        if (k < 2) {
            DepthUse& d = u->depths[k];
            if (depthTested && !d.tested) d.firstTestedSeq = seq;
            d.tested = d.tested || depthTested;
            d.written = d.written || writesDepth;
        }
    }
    if (texture.kind == TextureSource::Kind::Target) {
        uint32_t k = 0;
        while (k < u->sampledCount && u->sampled[k].target != texture.id) ++k;
        if (k == u->sampledCount) {
            if (k == 8) {
                b.overflow = true;
                return;
            }
            u->sampled[u->sampledCount++].target = texture.id;
        }
        SampleUse& smp = u->sampled[k];
        ++smp.draws;
        if (writtenBefore) {
            if (!smp.written) smp.written = writtenBefore;
            ++smp.drawsAfterWrite;
        }
    }
}

// At the view's ViewEnd (or its frame's end, if it never came): the principal, the auxiliary targets, the split reasons
// (1) a second scene: another target whose own depth buffer the view's draws both test and write (a test-only pass, the
// sky's 64 x 64 one, is auxiliary), (2) another colour target testing or writing memory of the principal depth, (3) another
// target (or a depth it writes) overlapping the principal's memory, (4) the principal colour or depth sampled (every draw
// of the view comes before its Light command), (5) a fifth auxiliary target; a full summary splits it too (counted apart);
// and the auxiliary targets a principal draw sampled after a write overlapping them, in draws. All by memory.
void GsFrontend::finishBinding(ViewBinding& b) {
    if (b.decided || !b.bound || !b.useCount) return;
    b.decided = true;
    auto split = [&](int reason, uint32_t other) {
        if (!(b.reasons & (1u << reason))) {
            b.reasons |= static_cast<uint8_t>(1u << reason);
            if (reason <= 5) ++stats.splitByReason[reason];
            else ++stats.splitSummaryFull;
        }
        if (!b.split) b.other = other;
        b.split = true;
    };
    const TargetUse* p = &b.uses[0];   // the first written, unless a depth-tested one came
    for (uint32_t i = 0; i < b.useCount; ++i)
        if (b.uses[i].tested && (!p->tested || b.uses[i].testedSeq < p->testedSeq)) p = &b.uses[i];
    b.target = p->colour;
    b.depth = 0;
    uint32_t first = 0;   // the principal's depth: that of its first depth-tested draw
    for (uint32_t k = 0; k < p->depthCount; ++k)
        if (p->depths[k].tested && (!first || p->depths[k].firstTestedSeq < first)) {
            first = p->depths[k].firstTestedSeq;
            b.depth = p->depths[k].target;
        }
    if (b.overflow) split(6, 0);
    auto principal = [&](uint32_t t) { return overlaps(t, b.target) || (b.depth && overlaps(t, b.depth)); };
    for (uint32_t i = 0; i < b.useCount; ++i) {
        const TargetUse& u = b.uses[i];
        for (uint32_t k = 0; k < u.sampledCount; ++k)   // rule 4: the principal sampled by any draw of the view
            if (principal(u.sampled[k].target)) split(4, u.sampled[k].target);
        if (&u == p) continue;
        b.auxDraws += u.draws;
        if (b.auxCount == 4) split(5, u.colour);
        else b.aux[b.auxCount++] = u.colour;
        for (uint32_t k = 0; k < u.depthCount; ++k) {
            const DepthUse& d = u.depths[k];
            const bool onPrincipalDepth = b.depth && overlaps(d.target, b.depth);
            if (d.tested && d.written && !onPrincipalDepth) split(1, u.colour);                     // rule 1: a second scene
            if (onPrincipalDepth && (d.tested || d.written)) split(2, u.colour);                    // rule 2
            if (d.written && overlaps(d.target, b.target)) split(3, d.target);                      // rule 3 (its depth)
        }
        if (principal(u.colour)) split(3, u.colour);                                                // rule 3
    }
    for (uint32_t k = 0; k < p->sampledCount; ++k) {   // auxiliary reads: principal draws sampling one after its write
        const SampleUse& smp = p->sampled[k];
        if (!smp.written) continue;
        for (uint8_t i = 0; i < b.auxCount; ++i)
            if (b.aux[i] == smp.written) {
                b.auxReads |= static_cast<uint8_t>(1u << i);
                b.auxReadDraws += smp.drawsAfterWrite;
            }
    }
}

// N4 (PLAN.md): a draw into `colour` inside the flare's or glare's bracket is marked when it draws onto the picture: the
// main view's principal target (decided at its ViewEnd, before the flare pass), or a screen-sized target by sceneRead's
// rule (the display buffer after the post effects, named only by the display record, later). The flare's samples go into
// a small target the game reads back: never marked. The flare's bracket wins should both be open. A main view with no
// tagged draw is never decided (finishBinding): its draws in a bracket fall to the size rule (its principal would be
// screen-sized anyway). Outside a scene frame nothing is marked (the brackets close with it: sceneFrame).
// The displayed buffer itself is named by the display records: a target the display showed in the last 8 VSyncs is the
// picture (rule 3). The race's displayed buffer is a 640 x 224 field (the flare's glows go there, after the scene's copy
// and before the HUD; nothing samples it after), which the size rule takes only when the menus had drawn it 448 tall
// before the race, not when a mid-run recording first sees it in the race. (A retired target whose id is reused stays
// "shown" up to 8 frames: harmless, as the brackets are open only during the flare and glare passes.)
uint8_t GsFrontend::bracketFor(uint32_t colour, uint8_t& rule) const {
    rule = 0;
    const uint8_t open = (sceneBrackets >> kBracketFlare) & 1 ? kBracketFlare : (sceneBrackets >> kBracketGlare) & 1 ? kBracketGlare : 0;
    if (!open || !colour || !sceneOpen) return 0;
    for (const uint16_t id : sceneMainViews)
        if (id <= viewBindings.size() && viewBindings[id - 1u].decided && viewBindings[id - 1u].target == colour) {
            rule = 1;
            return open;
        }
    if (colour <= targets.size()) {
        const Target& t = targets[colour - 1];
        if (!t.depth && t.bw * 64 >= 512 && t.height >= 384) {
            rule = 2;
            return open;
        }
    }
    for (const ShownTarget& s : shownTargets)
        if (s.id == colour && s.at + 8 >= frameCount) {
            rule = 3;
            return open;
        }
    return 0;
}

// A new Draw made inside a bracket, counted by the rule that marked it (or kept).
void GsFrontend::countBracket(uint8_t rule) {
    const uint8_t open = (sceneBrackets >> kBracketFlare) & 1 ? kBracketFlare : (sceneBrackets >> kBracketGlare) & 1 ? kBracketGlare : 0;
    if (!open || !sceneOpen) return;
    (rule == 1 ? stats.bracketPrincipal : rule == 2 ? stats.bracketScreen : rule == 3 ? stats.bracketDisplayed : stats.bracketKept)[open]++;
}

void GsFrontend::sceneRecord(const uint8_t* payload, size_t bytes) {
    gfx::scene::Prefix prefix;
    if (bytes < sizeof prefix) return;
    std::memcpy(&prefix, payload, sizeof prefix);
    if (prefix.kind == gfx::scene::kFrameBegin) {   // (a FrameBegin before the last one's end: that one is dropped, as the decoder does)
        sceneOpen = true;
        viewBindings.clear();
        sceneCuts = sceneCutsAsync = 0;
        sceneMarkerCommands.clear();
        sceneVsyncInFrame = false;
        sceneBrackets = 0;
        sceneMainViews.clear();
        reflectionFrameTargets.clear();
        glowOrigins.clear();
        // B6: a FrameBegin without the last frame's FrameEnd drops that frame (the decoder does): its jobs and counters go
        clearSceneJobs();
        sceneCacheCounters = {};
        sceneViewsFrom = frame.views.size();
    } else if (prefix.kind == gfx::scene::kMarker && sceneOpen) {   // (SceneFrame::markerCommands, a.8)
        sceneMarkerCommands.push_back(static_cast<uint32_t>(frame.commands.size()));
        if (bytes >= sizeof(gfx::scene::Marker)) {   // N4: the flare's and glare's brackets, in stream order (a cut list too)
            gfx::scene::Marker m;
            std::memcpy(&m, payload, sizeof m);
            switch (m.marker) {
            case gfx::scene::kMarkFlareBegin: sceneBrackets |= 1u << kBracketFlare; break;
            case gfx::scene::kMarkFlareEnd: sceneBrackets &= ~(1u << kBracketFlare); break;
            case gfx::scene::kMarkGlareBegin: sceneBrackets |= 1u << kBracketGlare; break;
            case gfx::scene::kMarkGlareEnd: sceneBrackets &= ~(1u << kBracketGlare); break;
            default: break;
            }
        }
    } else if (prefix.kind == gfx::scene::kViewBegin && bytes >= sizeof(gfx::scene::ViewBegin) && sceneOpen) {
        gfx::scene::ViewBegin begin;
        std::memcpy(&begin, payload, sizeof begin);
        if (begin.role == gfx::scene::kViewMain && begin.view && begin.view <= kViews &&
            std::find(sceneMainViews.begin(), sceneMainViews.end(), begin.view) == sceneMainViews.end())
            sceneMainViews.push_back(begin.view);   // (N4: its principal target, once decided, is the picture)
    } else if (prefix.kind == gfx::scene::kViewEnd && bytes >= sizeof(gfx::scene::ViewEnd) && sceneOpen) {
        gfx::scene::ViewEnd end;
        std::memcpy(&end, payload, sizeof end);
        if (end.view && end.view <= kViews) {
            if (viewBindings.size() < end.view) viewBindings.resize(end.view);
            viewBindings[end.view - 1u].ended = true;
            finishBinding(viewBindings[end.view - 1u]);   // (a.5.7: decided at ViewEnd, against the final principal)
        }
    }
}

void GsFrontend::sceneView(std::shared_ptr<const scene::SceneView> view) {
    if (!view) return;
    FrameList::ViewOnList v;   // with its binding as it stands at its ViewEnd (every tagged draw of it has come)
    const uint16_t id = view->view.id;
    if (id && id <= viewBindings.size() && viewBindings[id - 1u].bound) {
        const ViewBinding& b = viewBindings[id - 1u];
        v.target = b.target;
        v.depth = b.depth;
        v.splitTargets = b.split;
        v.auxCount = b.auxCount;
        for (uint8_t i = 0; i < b.auxCount; ++i) v.auxTargets[i] = b.aux[i];
        v.auxReads = b.auxReads;
        v.auxDraws = static_cast<uint16_t>(std::min<uint32_t>(b.auxDraws, 0xFFFF));
    }
    // B6 (design a92c4a2): each instance's mesh and materials, from its tagged jobs in this list (all of them are here
    // by its ViewEnd, a.5.3, unless the view was cut: then it is never substituted, and gets none).
    // A view whose own checks failed at its ViewEnd (view.valid) is never substituted either: none.
    const bool cut = id && id <= viewBindings.size() && viewBindings[id - 1u].cut;
    if (sceneSplit && !cut && view->view.valid) {
        const auto started = std::chrono::steady_clock::now();
        const double cpuStarted = scene::threadCpuMicroseconds();
        if (!sceneCache_) {
            sceneCache_ = std::make_shared<scene::SceneCache>();
            if (sceneClassifier) sceneCache_->setClassifier(sceneClassifier);   // (F2b's glow class)
            static const bool cacheLog = [] { const char* e = std::getenv("GT4_SCENE_CACHE_LOG"); return e && *e == '1'; }();
            sceneCache_->setTrackObjects(cacheLog);   // (the "same ids" check: B6's acceptance reads it with the log)
        }
        if (!frame.spareIds.empty()) {
            v.ids = std::move(frame.spareIds.back());
            frame.spareIds.pop_back();
        }
        if (!frame.spareMaterials.empty()) {
            v.materials = std::move(frame.spareMaterials.back());
            frame.spareMaterials.pop_back();
        }
        if (!frame.spareItems.empty()) {
            v.items = std::move(frame.spareItems.back());
            frame.spareItems.pop_back();
        }
        v.ids.resize(view->instances.size());
        for (size_t k = 0; k < view->instances.size(); ++k) {
            const scene::InstanceData& instance = view->instances[k];
            const uint32_t tag = instance.index + 1;   // (the jobs' pad[0]: its position, not its serial)
            sceneItems.clear();
            for (uint32_t at = tag < sceneJobFirst.size() ? sceneJobFirst[tag] : UINT32_MAX; at != UINT32_MAX; at = sceneJobs[at].next) {
                const SceneJob& sj = sceneJobs[at];
                const gfx::ModelJob& job = frame.models[sj.job];
                const Draw& draw = frame.draws[sj.draw];
                scene::ItemSource item;
                item.batch = {&job, frame.modelData.data() + job.data};
                item.prim = draw.state.prim;
                item.context = &draw.state.c;
                item.texAlpha0 = draw.state.texAlpha0;
                item.texAlpha1 = draw.state.texAlpha1;
                item.texBlackTransparent = draw.state.texBlackTransparent;
                item.perPixelBlend = draw.state.perPixelBlend;
                if (draw.texture.kind == TextureSource::Kind::Memory) item.texture = textureIdentity(draw.texture.id);
                else if (draw.texture.kind == TextureSource::Kind::Target) item.texture = 0x7A26E70000000000ull ^ draw.texture.id;
                else if (draw.texture.kind == TextureSource::Kind::Reinterpreted && draw.texture.id < frame.reinterpretations.size()) {
                    // (which target, read through which format and palette: not the per-list index)
                    const Reinterpretation& ri = frame.reinterpretations[draw.texture.id];
                    const gfx::Texture& tx = ri.texture;
                    item.texture = mix(mix(mix(0x5E1D000000000000ull, ri.target), uint64_t(tx.format) | uint64_t(tx.log2Width) << 8 |
                                                                              uint64_t(tx.log2Height) << 16 | uint64_t(tx.clutFormat) << 24 |
                                                                              uint64_t(tx.clutOffset) << 32),
                                       uint64_t(tx.base) | uint64_t(tx.clutBase) << 16 | uint64_t(tx.width) << 32);
                }
                sceneItems.push_back(item);
                v.items.push_back({sj.job, sj.draw, sj.firstVertex, sj.vertexCount});   // (parallel to the materials assign makes)
            }
            if (std::getenv("GT4_N3_LOG")) {   // LOCAL N3 DEBUG, never committed: each layer-state item's vertex light, once
                static std::unordered_set<uint64_t> seenItems;
                for (const scene::ItemSource& it : sceneItems) {
                    const gfx::Context* cx = it.context;
                    if (!cx || !it.batch.job || !it.batch.words) continue;
                    const bool state = ((it.prim >> 4) & 1) && ((it.prim >> 6) & 1) && cx->blendA == 0 && cx->blendB == 1 &&
                                       cx->blendC == 0 && cx->blendD == 1 && !cx->alphaTest && cx->depthTest && cx->depthFunc == 2 &&
                                       !cx->depthWrite && (it.batch.job->chain >> 16) == 0;
                    if (!state) continue;
                    uint64_t key = it.texture;
                    for (uint32_t w = 0; w < 64 * 4 && w < gfx::kModelWords; ++w) key = key * 1099511628211ull ^ it.batch.words[w];
                    if (!seenItems.insert(key).second) continue;
                    const scene::DecodedBatch b = scene::decodeBatch(*it.batch.job, it.batch.words);
                    const gfx::ModelJob& jb = *it.batch.job;
                    double lo = 1e9, hi = -1, sum = 0;
                    uint32_t base = 0, peak = 0, over = 0, above = 0;
                    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
                    auto lumOf = [](uint32_t c) { return 0.2126 * (c & 0xFF) + 0.7152 * ((c >> 8) & 0xFF) + 0.0722 * ((c >> 16) & 0xFF); };
                    for (const scene::MeshVertex& mv : b.vertices) {
                        const double l = lumOf(mv.colour);
                        if (l < lo) lo = l, base = mv.colour;
                        if (l > hi) hi = l, peak = mv.colour;
                        sum += l;
                        if ((mv.colour & 0xFF) > 0x80 || ((mv.colour >> 8) & 0xFF) > 0x80 || ((mv.colour >> 16) & 0xFF) > 0x80) ++over;
                        float clip[4];
                        for (int q = 0; q < 4; ++q)
                            clip[q] = jb.matrix[0][q] * mv.position[0] + jb.matrix[1][q] * mv.position[1] + jb.matrix[2][q] * mv.position[2] + jb.matrix[3][q];
                        if (clip[3] > 1e-3f) {
                            const float sxp = jb.offset[0] + jb.scale[0] * clip[0] / clip[3] - jb.originX, syp = jb.offset[1] + jb.scale[1] * clip[1] / clip[3] - jb.originY;
                            x0 = std::min(x0, sxp); x1 = std::max(x1, sxp); y0 = std::min(y0, syp); y1 = std::max(y1, syp);
                        }
                    }
                    for (const scene::MeshVertex& mv : b.vertices) above += lumOf(mv.colour) > lo + 4.0 ? 1 : 0;
                    const float area = x1 > x0 ? (std::min(x1, 640.0f) - std::max(x0, 0.0f)) * (std::min(y1, 448.0f) - std::max(y0, 0.0f)) : 0.0f;
                    std::printf("N3 item: host texture %u vertices %zu above-base %u over-0x80 %u base %06x peak %06x lum %.1f..%.1f mean %.1f area %.0f\n",
                                debugTextureIdOf(it.texture), b.vertices.size(), above, over, base & 0xFFFFFF, peak & 0xFFFFFF, lo, hi,
                                b.vertices.empty() ? 0.0 : sum / double(b.vertices.size()), std::max(0.0f, area));
                    double n3Share = 0, n3Width = 0, n3dPds = 0, n3dPdt = 0;   // (for the v4 judgement below)
                    {   // LOCAL N3 DEBUG: the item's triangles in world space (row vectors: [p 1] * objectToWorld), area by |n| per axis
                        const auto& M = instance.objectToWorld.m;
                        double total = 0, axis[3] = {}, bins[3][10] = {};
                        size_t tris = 0;
                        for (size_t t = 0; t + 2 < b.triangles.size(); t += 3) {
                            double w[3][3];
                            for (int c = 0; c < 3; ++c) {
                                const float* p = b.vertices[b.triangles[t + c]].position;
                                for (int q = 0; q < 3; ++q) w[c][q] = p[0] * M[0][q] + p[1] * M[1][q] + p[2] * M[2][q] + M[3][q];
                            }
                            const double e1[3] = {w[1][0] - w[0][0], w[1][1] - w[0][1], w[1][2] - w[0][2]};
                            const double e2[3] = {w[2][0] - w[0][0], w[2][1] - w[0][1], w[2][2] - w[0][2]};
                            const double n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
                            const double len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                            if (!(len > 0) || !std::isfinite(len)) continue;
                            ++tris;
                            total += len / 2;
                            for (int q = 0; q < 3; ++q) {
                                const double c = std::fabs(n[q]) / len;
                                axis[q] += len / 2 * c;
                                bins[q][std::min(9, int(c * 10))] += len / 2;
                            }
                        }
                        double dev = 0, scl[3];
                        for (int i = 0; i < 3; ++i) {
                            scl[i] = std::sqrt(double(M[i][0]) * M[i][0] + double(M[i][1]) * M[i][1] + double(M[i][2]) * M[i][2]);
                            for (int q = 0; q < 3; ++q) dev = std::max(dev, std::fabs((scl[i] > 0 ? M[i][q] / scl[i] : 0.0) - (i == q ? 1.0 : 0.0)));
                        }
                        const double inv = total > 0 ? 1.0 / total : 0.0;
                        n3Share = bins[1][9] * inv;
                        std::printf("N3 orient: host texture %u flags %04x rotdev %.3f scale %.3f,%.3f,%.3f triangles %zu area %.1f axis %.3f,%.3f,%.3f",
                                    debugTextureIdOf(it.texture), unsigned(instance.flags), dev, scl[0], scl[1], scl[2], tris, total,
                                    axis[0] * inv, axis[1] * inv, axis[2] * inv);
                        for (int q = 0; q < 3; ++q) {
                            std::printf(" bins%c", "xyz"[q]);
                            for (int j = 0; j < 10; ++j) std::printf("%c%.3f", j ? ',' : ' ', bins[q][j] * inv);
                        }
                        std::printf("\n");
                    }
                    {   // LOCAL N3 DEBUG: the footprint of the item's up-facing triangles (|n.y| >= 0.9), world space
                        const auto& M = instance.objectToWorld.m;
                        const size_t nv = b.vertices.size();
                        std::vector<std::array<double, 3>> P(nv);
                        for (size_t i = 0; i < nv; ++i) {
                            const float* p = b.vertices[i].position;
                            for (int q = 0; q < 3; ++q) P[i][q] = p[0] * M[0][q] + p[1] * M[1][q] + p[2] * M[2][q] + M[3][q];
                        }
                        std::map<std::array<long long, 3>, uint32_t> weld;   // vertices at one position (1 mm) are one
                        std::vector<uint32_t> rep(nv), parent(nv);
                        for (size_t i = 0; i < nv; ++i) {
                            const std::array<long long, 3> key{std::llround(P[i][0] * 1000), std::llround(P[i][1] * 1000), std::llround(P[i][2] * 1000)};
                            rep[i] = weld.emplace(key, uint32_t(i)).first->second;
                        }
                        std::iota(parent.begin(), parent.end(), 0u);
                        auto find = [&](uint32_t x) { while (parent[x] != x) x = parent[x] = parent[parent[x]]; return x; };
                        struct UpTri { uint32_t v[3]; double area; };
                        std::vector<UpTri> up;
                        double sAcc = 0, tAcc = 0, uvArea = 0;
                        for (size_t t = 0; t + 2 < b.triangles.size(); t += 3) {
                            const uint32_t a0 = b.triangles[t], a1 = b.triangles[t + 1], a2 = b.triangles[t + 2];
                            const double e1[3] = {P[a1][0] - P[a0][0], P[a1][1] - P[a0][1], P[a1][2] - P[a0][2]};
                            const double e2[3] = {P[a2][0] - P[a0][0], P[a2][1] - P[a0][1], P[a2][2] - P[a0][2]};
                            const double n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
                            const double len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                            if (!(len > 0) || !std::isfinite(len) || std::fabs(n[1]) / len < 0.9) continue;
                            up.push_back({{rep[a0], rep[a1], rep[a2]}, len / 2});
                            parent[find(rep[a1])] = find(rep[a0]);
                            parent[find(rep[a2])] = find(rep[a0]);
                            const float* s0 = b.vertices[a0].st; const float* s1 = b.vertices[a1].st; const float* s2 = b.vertices[a2].st;
                            const double ds1 = s1[0] - s0[0], dt1 = s1[1] - s0[1], ds2 = s2[0] - s0[0], dt2 = s2[1] - s0[1];
                            const double det = ds1 * dt2 - ds2 * dt1;
                            if (std::fabs(det) > 1e-12) {   // dP/ds and dP/dt: world units per unit of S and T
                                double ps = 0, pt = 0;
                                for (int q = 0; q < 3; ++q) {
                                    const double gs = (dt2 * e1[q] - dt1 * e2[q]) / det, gt = (-ds2 * e1[q] + ds1 * e2[q]) / det;
                                    ps += gs * gs;
                                    pt += gt * gt;
                                }
                                sAcc += len / 2 * std::sqrt(ps);
                                tAcc += len / 2 * std::sqrt(pt);
                                uvArea += len / 2;
                            }
                        }
                        std::map<std::pair<uint32_t, uint32_t>, int> edges;
                        for (const UpTri& u : up)
                            for (int k = 0; k < 3; ++k) {
                                uint32_t x = u.v[k], y = u.v[(k + 1) % 3];
                                if (x > y) std::swap(x, y);
                                ++edges[{x, y}];
                            }
                        struct Comp { double area = 0, perimeter = 0; std::vector<uint32_t> verts; };
                        std::map<uint32_t, Comp> comps;
                        for (const UpTri& u : up) {
                            Comp& c = comps[find(u.v[0])];
                            c.area += u.area;
                            for (uint32_t v : u.v) c.verts.push_back(v);
                        }
                        for (const auto& [e, count] : edges)
                            if (count == 1) {
                                const double dx = P[e.first][0] - P[e.second][0], dz = P[e.first][2] - P[e.second][2];
                                comps[find(e.first)].perimeter += std::sqrt(dx * dx + dz * dz);
                            }
                        double area = 0, width = 0, pcaWidth = 0, longest = 0;
                        for (auto& [root, c] : comps) {
                            double mx = 0, mz = 0;
                            for (uint32_t v : c.verts) mx += P[v][0], mz += P[v][2];
                            mx /= double(c.verts.size()); mz /= double(c.verts.size());
                            double sxx = 0, szz = 0, sxz = 0;
                            for (uint32_t v : c.verts) {
                                const double dx = P[v][0] - mx, dz = P[v][2] - mz;
                                sxx += dx * dx; szz += dz * dz; sxz += dx * dz;
                            }
                            const double th = 0.5 * std::atan2(2 * sxz, sxx - szz);   // the major axis
                            const double ux = std::cos(th), uz = std::sin(th);
                            double a0 = 1e30, a1 = -1e30, b0 = 1e30, b1 = -1e30;
                            for (uint32_t v : c.verts) {
                                const double dx = P[v][0] - mx, dz = P[v][2] - mz;
                                const double pa = dx * ux + dz * uz, pb = -dx * uz + dz * ux;
                                a0 = std::min(a0, pa); a1 = std::max(a1, pa); b0 = std::min(b0, pb); b1 = std::max(b1, pb);
                            }
                            const double minor = std::min(a1 - a0, b1 - b0), major = std::max(a1 - a0, b1 - b0);
                            area += c.area;
                            width += c.area * (c.perimeter > 0 ? 2 * c.area / c.perimeter : 0.0);
                            pcaWidth += c.area * minor;
                            longest = std::max(longest, major);
                        }
                        const double inv = area > 0 ? 1.0 / area : 0.0, uvInv = uvArea > 0 ? 1.0 / uvArea : 0.0;
                        std::printf("N3 footprint: host texture %u pieces %zu area %.3f width %.3f pcawidth %.3f longest %.2f dPds %.3f dPdt %.3f\n",
                                    debugTextureIdOf(it.texture), comps.size(), area, width * inv, pcaWidth * inv, longest, sAcc * uvInv,
                                    tAcc * uvInv);
                        n3Width = width * inv;
                        n3dPds = sAcc * uvInv;
                        n3dPdt = tAcc * uvInv;
                    }
                    if (std::getenv("GT4_N3_V4SKIP")) {   // LOCAL N3 DEBUG: v4 judged per item at first sight; its texture skipped from then on
                        double cs[4] = {};
                        for (const scene::MeshVertex& mv : b.vertices)
                            for (int k = 0; k < 4; ++k) cs[k] += double((mv.colour >> (8 * k)) & 0xFF);
                        const double nv = b.vertices.empty() ? 1.0 : double(b.vertices.size());
                        const double vcLum = (0.2126 * cs[0] + 0.7152 * cs[1] + 0.0722 * cs[2]) / nv, va = cs[3] / nv;
                        scene::TextureSummary ts;
                        const bool known = textureSummary(it.texture, ts) && ts.width && ts.height;
                        const double texel = known ? std::max(n3dPds / ts.width, n3dPdt / ts.height) : 0.0;
                        const bool v4 = vcLum > 16 && va >= 100 && known && ts.centreToRim < 3 && n3Share >= 0.9 && n3Width >= 0.9 &&
                                        texel >= 0.03;
                        // LOCAL N3 DEBUG (d): GT4_N3_V4NARROW=C keeps a v4 item whose texture is neutral (its mean colour's
                        // max - min under C: the broad white layer), so only coloured pools are skipped
                        static const double narrow = [] { const char* e = std::getenv("GT4_N3_V4NARROW"); return e ? std::atof(e) : -1.0; }();
                        const int chroma = known ? std::max({ts.mean[0], ts.mean[1], ts.mean[2]}) - std::min({ts.mean[0], ts.mean[1], ts.mean[2]}) : 0;
                        const bool neutralKept = v4 && narrow >= 0 && chroma < narrow;
                        if (v4 && !neutralKept) n3SkipIdentities.insert(it.texture);
                        std::printf("N3 v4: host texture %u identity %016llx vc %.1f a %.1f c/r %.2f share %.3f width %.3f texel %.4f -> %s\n",
                                    debugTextureIdOf(it.texture), static_cast<unsigned long long>(it.texture), vcLum, va,
                                    known ? ts.centreToRim : -1.0f, n3Share, n3Width, texel,
                                    neutralKept ? "keep (neutral)" : (v4 ? "skip" : "keep"));
                    }
                }
            }
            v.ids[k] = sceneCache_->assign(instance, sceneItems.data(), sceneItems.size(), v.materials, sceneCacheCounters);
            ++stats.sceneInstances;
            stats.sceneItems += sceneItems.size();
        }
        stats.sceneCacheMicroseconds += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - started).count();
        stats.sceneCacheCpuMicroseconds += scene::threadCpuMicroseconds() - cpuStarted;
        frame.sceneCache = sceneCache_;   // (a list cut before its FrameEnd carries views with ids: their cache with them)
    }
    v.package = std::move(view);
    frame.views.push_back(std::move(v));
    ++stats.scenePackages;
}

// A7's check (PLAN A7): each glow the flare pass drew onto the picture this frame has a Light at its centre. A glow's job
// matrix is object -> clip, and 00498198 draws it at the light's view position pulled along its ray (pos x (z - 0.5) / z),
// so its origin lands where the Light does through the main view's projection. Lights of the frame's main views only (the
// glow pass draws in the main view's space).
void GsFrontend::checkGlows(const scene::SceneFrame& f) {
    if (glowOrigins.empty()) return;
    glowLights.clear();
    for (const scene::ViewData& v : f.views) {
        if (v.role != gfx::scene::kViewMain || !v.projection || v.projection > f.projections.size()) continue;
        const scene::Mat4& p = f.projections[v.projection - 1u].viewToClip;
        for (uint32_t k = v.firstLight; k < v.firstLight + v.lightCount && k < f.lights.size(); ++k) {
            const float* q = f.lights[k].position;
            float c[4];
            for (int j = 0; j < 4; ++j) c[j] = q[0] * p.m[0][j] + q[1] * p.m[1][j] + q[2] * p.m[2][j] + p.m[3][j];
            if (c[3] > 0) glowLights.emplace_back(c[0] / c[3], c[1] / c[3]);
        }
    }
    for (const GlowOrigin& g : glowOrigins) {
        if (!(g.w > 0)) continue;
        ++stats.glowsChecked;
        const float x = g.x / g.w, y = g.y / g.w;
        float nearest = INFINITY;
        for (const auto& l : glowLights) nearest = std::min(nearest, std::max(std::fabs(l.first - x), std::fabs(l.second - y)));
        if (nearest <= 0.02f) {
            ++stats.glowsWithLight;
            stats.glowMaxDistance = std::max(stats.glowMaxDistance, nearest);
        }
    }
    glowOrigins.clear();
}

void GsFrontend::sceneFrame(std::unique_ptr<scene::SceneFrame> f) {
    if (!f) return;
    scene::SceneCounters& c = f->counters;
    for (ViewBinding& b : viewBindings) finishBinding(b);   // (views whose ViewEnd never came)
    for (scene::ViewData& v : f->views) {
        const ViewBinding* b = v.id && v.id <= viewBindings.size() ? &viewBindings[v.id - 1u] : nullptr;
        if (b && b->bound) {
            v.target = b->target;
            v.depth = b->depth;
            v.splitTargets = b->split;
            v.cut = b->cut;
            if (b->split) ++c.viewSplitTargets, ++stats.splitByRole[v.role & 15];
            c.viewAuxiliaryDraws += b->auxDraws;
            c.viewAuxiliaryReads += b->auxReadDraws;
            stats.auxDrawsByRole[v.role & 15] += b->auxDraws;
            stats.auxReadsByRole[v.role & 15] += b->auxReadDraws;
            // GT4_SCENE_DEBUG=1: the first test-only auxiliary depth of each role (tested, never written: auxiliary by rule 1;
            // were it to move into the principal, rules 2 and 3 split the view)
            static const bool debugAux = [] { const char* e = std::getenv("GT4_SCENE_DEBUG"); return e && e[0] == '1'; }();
            static bool shownAux[16] = {};
            if (debugAux && !shownAux[v.role & 15])
                for (uint32_t i = 0; i < b->useCount; ++i) {
                    const TargetUse& u = b->uses[i];
                    if (u.colour == b->target) continue;
                    for (uint32_t k = 0; k < u.depthCount; ++k)
                        if (u.depths[k].tested && !u.depths[k].written && !shownAux[v.role & 15]) {
                            shownAux[v.role & 15] = true;
                            const Target* zt = nullptr;
                            for (const Target& t : targets)
                                if (t.id == u.depths[k].target) zt = &t;
                            std::printf("scene: view %u role %u: auxiliary target %u tests depth %u (ZBP %u), never writes it\n", v.id,
                                        v.role, u.colour, u.depths[k].target, zt ? zt->bp : 0u);
                        }
                }
            // GT4_SCENE_DEBUG=1: the first split of each role, with both targets (bp, width, format, height)
            static const bool debug = [] { const char* e = std::getenv("GT4_SCENE_DEBUG"); return e && e[0] == '1'; }();
            static bool shown[16] = {};
            if (debug && b->split && !shown[v.role & 15]) {
                shown[v.role & 15] = true;
                auto describe = [&](uint32_t id) {
                    for (const Target& t : targets)
                        if (t.id == id) return std::to_string(id) + " (bp " + std::to_string(t.bp) + " bw " + std::to_string(t.bw) + " psm " +
                                               std::to_string(t.psm) + " h " + std::to_string(t.height) + ")";
                    return std::to_string(id);
                };
                std::string uses;
                for (uint32_t i = 0; i < b->useCount; ++i) {
                    const TargetUse& u = b->uses[i];
                    uses += " [" + describe(u.colour) + " draws " + std::to_string(u.draws) + (u.tested ? " tested" : "") + " depths";
                    for (uint32_t k = 0; k < u.depthCount; ++k)
                        uses += " " + describe(u.depths[k].target) + (u.depths[k].tested ? "t" : "") + (u.depths[k].written ? "w" : "");
                    uses += " samples";
                    for (uint32_t k = 0; k < u.sampledCount; ++k) uses += " " + std::to_string(u.sampled[k].target);
                    uses += "]";
                }
                std::printf("scene: view %u role %u split (reasons %x): principal %s depth %s, then %s; uses%s\n", v.id, v.role,
                            b->reasons, describe(b->target).c_str(), describe(b->depth).c_str(), describe(b->other).c_str(), uses.c_str());
            }
            if (b->cut) ++c.viewsCut, ++stats.cutByRole[v.role & 15];
        } else {
            ++c.viewsUnbound;   // no tagged draw in it
            ++stats.unboundByRole[v.role & 15];
        }
    }
    c.cuts = sceneCuts;
    c.cutsAsync = sceneCutsAsync;
    // the markers' command indices (a.8), and A5a's order after the frame's first Light command
    f->markerCommands.assign(sceneMarkerCommands.begin(), sceneMarkerCommands.end());
    {
        uint32_t light = UINT32_MAX;
        for (uint32_t k = 0; k < frame.commands.size(); ++k)
            if (frame.commands[k].kind == FrameList::Command::Kind::Light) {
                light = k;
                break;
            }
        if (f->flags & gfx::scene::kFrameVehiclesOnly) {
            // (a.1 mode 2: Vehicles only, no markers sent: not a frame A5a's check is about)
        } else if (sceneCuts || sceneVsyncInFrame) {
            ++stats.markersNotChecked;   // (its Light and markers may be in an earlier list: a cut or a VSync inside the frame)
        } else if (light != UINT32_MAX) {
            if (f->markerCommands.size() != f->markers.size()) {
                ++stats.markersNotChecked;   // (markers the front end did not see)
            } else {
                // the markers' kinds in order: one or more Post pairs, then one or more Hud pairs (split screen: one a
                // player), each Begin followed by its End, indices non-decreasing, the first Light before the first PostEnd
                bool post = false, hud = false, ordered = true;   // (a Post / HUD pair begun: present, complete or not)
                uint16_t open = 0;
                uint32_t last = 0, firstPostBegin = UINT32_MAX, firstPostEnd = UINT32_MAX;
                for (size_t m = 0; m < f->markers.size(); ++m) {
                    const uint16_t kind = f->markers[m].marker;
                    if (kind < 1 || kind > 4) continue;   // (other markers: not A5a's)
                    const uint32_t at = f->markerCommands[m];
                    if (at < last) ordered = false;
                    last = at;
                    if (kind == 1 || kind == 3) {   // a Begin: none open, and no Post after the HUD
                        if (open || (kind == 1 && hud)) ordered = false;
                        open = kind;
                        (kind == 1 ? post : hud) = true;
                        if (kind == 1 && firstPostBegin == UINT32_MAX) firstPostBegin = at;
                    } else {                        // an End: its Begin open
                        if (open != kind - 1) ordered = false;
                        open = 0;
                        if (kind == 2 && firstPostEnd == UINT32_MAX) firstPostEnd = at;
                    }
                }
                if (open) ordered = false;
                if (!post || !hud) {
                    ++stats.markersAbsent;   // (no PostBegin or no HudBegin: the garage car, RaceSolitaire's own panel)
                } else {
                    ordered = ordered && light < firstPostEnd;
                    ++(ordered ? stats.markersInOrder : stats.markersOutOfOrder);
                    if (ordered) ++(light < firstPostBegin ? stats.lightBeforePost : stats.lightInsidePost);
                }
            }
        }
    }
    checkGlows(*f);
    ++stats.sceneFrames;
    stats.viewSplitTargets += c.viewSplitTargets;
    stats.viewsUnbound += c.viewsUnbound;
    stats.cuts += c.cuts;
    stats.cutsAsync += c.cutsAsync;
    stats.viewsCut += c.viewsCut;
    // B6: the ids of this list's views into the frame's instances (by serial), for SceneFrame-wide readers; views on an
    // earlier partial list keep theirs there. Then the counters, and the caches' frame end.
    for (size_t at = std::min(sceneViewsFrom, frame.views.size()); at < frame.views.size(); ++at) {   // (this frame's)
        const FrameList::ViewOnList& v = frame.views[at];
        if (!v.package || v.ids.size() != v.package->instances.size()) continue;
        for (size_t k = 0; k < v.ids.size(); ++k) {
            const uint32_t index = v.package->instances[k].index;
            if (index >= f->instances.size()) continue;
            scene::InstanceData& in = f->instances[index];
            const scene::InstanceIds& ids = v.ids[k];
            in.mesh = ids.mesh;
            in.firstMaterial = static_cast<uint32_t>(f->materials.size());
            in.materialCount = ids.materialCount;
            f->materials.insert(f->materials.end(), v.materials.begin() + ids.firstMaterial,
                                v.materials.begin() + ids.firstMaterial + ids.materialCount);
        }
    }
    c.meshesNew += sceneCacheCounters.meshesNew;
    c.meshesPending += sceneCacheCounters.meshesPending;
    c.materialsNew += sceneCacheCounters.materialsNew;
    c.meshesSkinned += sceneCacheCounters.meshesSkinned;
    c.meshesPerFrame += sceneCacheCounters.meshesPerFrame;
    c.meshesOutOfBinding += sceneCacheCounters.meshesOutOfBinding;
    stats.meshesNew += sceneCacheCounters.meshesNew;   // (the decoder totals its frames before these are filled)
    stats.meshesPending += sceneCacheCounters.meshesPending;
    stats.materialsNew += sceneCacheCounters.materialsNew;
    stats.meshesSkinned += sceneCacheCounters.meshesSkinned;
    stats.meshesPerFrame += sceneCacheCounters.meshesPerFrame;
    stats.meshesOutOfBinding += sceneCacheCounters.meshesOutOfBinding;
    // GT4_SCENE_CACHE_LOG=1 (B6's acceptance): one line a scene frame: its instances and items given ids, its distinct
    // materials, what the caches found, the front-end time it took.
    static const bool cacheLog = [] { const char* e = std::getenv("GT4_SCENE_CACHE_LOG"); return e && *e == '1'; }();
    if (cacheLog && sceneCache_) {
        static uint64_t lastInstances = 0, lastItems = 0;
        static double lastMicroseconds = 0.0, lastCpuMicroseconds = 0.0;
        std::vector<scene::MaterialId> distinct(f->materials);
        std::sort(distinct.begin(), distinct.end());
        distinct.erase(std::unique(distinct.begin(), distinct.end()), distinct.end());
        size_t meshes = 0;
        for (const scene::InstanceData& in : f->instances) meshes += in.mesh != 0;
        std::printf("scene cache: vblank %u: %llu instances (%zu with a mesh), %llu items, %zu materials; meshes new %llu, pending %llu, "
                    "skinned %llu, per-frame %llu; materials new %llu; %.0f us (cpu %.0f us)\n",
                    f->vblank, static_cast<unsigned long long>(stats.sceneInstances - lastInstances), meshes,
                    static_cast<unsigned long long>(stats.sceneItems - lastItems), distinct.size(),
                    static_cast<unsigned long long>(sceneCacheCounters.meshesNew), static_cast<unsigned long long>(sceneCacheCounters.meshesPending),
                    static_cast<unsigned long long>(sceneCacheCounters.meshesSkinned), static_cast<unsigned long long>(sceneCacheCounters.meshesPerFrame),
                    static_cast<unsigned long long>(sceneCacheCounters.materialsNew), stats.sceneCacheMicroseconds - lastMicroseconds,
                    stats.sceneCacheCpuMicroseconds - lastCpuMicroseconds);
        lastInstances = stats.sceneInstances;
        lastItems = stats.sceneItems;
        lastMicroseconds = stats.sceneCacheMicroseconds;
        lastCpuMicroseconds = stats.sceneCacheCpuMicroseconds;
    }
    sceneCacheCounters = {};
    if (sceneCache_) sceneCache_->endFrame();
    clearSceneJobs();   // (the next frame's instances count from 1 again)
    frame.scene = std::move(f);   // (two in one list: the newer stands; the stream counts that case)
    frame.sceneCache = sceneCache_;   // (F2b: the render thread reads the frame's ids through it; null before the split)
    sceneOpen = false;
    viewBindings.clear();
    sceneCuts = sceneCutsAsync = 0;
    if (sceneBrackets) ++stats.bracketsOpenAtEnd;   // (N4: an End marker missing; the bracket closes with the frame)
    sceneBrackets = 0;
    sceneMainViews.clear();
    // GT4_SCENE_DEBUG=1: the first frame's reflection targets (A6: the road reflection's 64 x 32, what else it writes)
    static const bool debugReflection = [] {
        const char* e = std::getenv("GT4_SCENE_DEBUG");
        return e && e[0] == '1';
    }();
    static bool shownReflection = false;
    if (debugReflection && !shownReflection && !reflectionFrameTargets.empty()) {
        shownReflection = true;
        for (const uint32_t id : reflectionFrameTargets)
            if (id && id <= targets.size()) {
                const Target& t = targets[id - 1];
                std::printf("scene: the road reflection writes target %u (bp %u bw %u psm %u h %u%s)\n", id, t.bp,
                            t.bw, t.psm, t.height, t.depth ? ", depth" : "");
            }
    }
    reflectionFrameTargets.clear();
}

void GsFrontend::vsync(uint64_t vblank, FrameList& out) {
    // R10 spike (local, not committed): GT4_R10_LOG=FILE writes one line per frame with the colour grading's weights
    // and curve and each scene target's fog colour.
    static FILE* r10 = [] { const char* p = std::getenv("GT4_R10_LOG"); return p ? std::fopen(p, "w") : nullptr; }();
    if (r10) {
        std::fprintf(r10, "%llu effects %zu", static_cast<unsigned long long>(vblank), frame.effects.size());
        for (const auto& e : frame.effects) {
            double ysum = 0;
            uint64_t h = 1469598103934665603ull;
            for (uint32_t c : e.curve) {
                ysum += 0.2126 * (c & 0xFF) + 0.7152 * ((c >> 8) & 0xFF) + 0.0722 * ((c >> 16) & 0xFF);
                h = (h ^ c) * 1099511628211ull;
            }
            std::fprintf(r10, " | w %06x %06x %06x curve %016llx meanY %.3f c64 %08x c128 %08x c192 %08x",
                         e.weights[0] & 0xFFFFFF, e.weights[1] & 0xFFFFFF, e.weights[2] & 0xFFFFFF,
                         static_cast<unsigned long long>(h), ysum / 256.0, e.curve[64], e.curve[128], e.curve[192]);
        }
        {   // the frame's 3D batches: how many, and a hash of their packets' identity (the first data word of each)
            uint64_t h = 1469598103934665603ull;
            std::vector<uint32_t> ids;
            for (const auto& m : frame.models) ids.push_back(m.data < frame.modelData.size() ? frame.modelData[m.data] : 0u);
            std::sort(ids.begin(), ids.end());
            for (uint32_t id : ids) h = (h ^ id) * 1099511628211ull;
            std::fprintf(r10, " ; batches %zu draws %zu set %016llx", frame.models.size(), frame.draws.size(),
                         static_cast<unsigned long long>(h));
        }
        {   // GT4_R10_DRAWS=FIRST,LAST: the frame's draws that are not 3D batches, by state, texture and target
            static unsigned long long dFirst = 0, dLast = 0;
            static bool dRead = false;
            if (!dRead) {
                dRead = true;
                if (const char* e = std::getenv("GT4_R10_DRAWS")) std::sscanf(e, "%llu,%llu", &dFirst, &dLast);
            }
            if (vblank >= dFirst && vblank <= dLast && dLast) {
                struct Agg { uint32_t draws = 0, verts = 0; double r = 0, g = 0, b = 0, a = 0; uint64_t tex = 1469598103934665603ull; bool modeled = false; };
                std::map<uint64_t, Agg> agg;
                for (const auto& d : frame.draws) {
                    uint64_t h = 1469598103934665603ull;
                    const auto* p8 = reinterpret_cast<const uint8_t*>(&d.state);
                    for (size_t i = 0; i < sizeof d.state; ++i) h = (h ^ p8[i]) * 1099511628211ull;
                    h = (h ^ (uint64_t(d.texture.kind) << 56) ^ (uint64_t(d.modeled) << 48) ^ d.target) * 1099511628211ull;
                    Agg& a = agg[h];
                    ++a.draws;
                    a.modeled = d.modeled;
                    // the textures it used, in draw order: id and content version (a swap or a rewrite shows here)
                    a.tex = (a.tex ^ ((uint64_t(d.texture.id) << 32) | d.texture.version)) * 1099511628211ull;
                    for (uint32_t v = d.first; v < d.first + d.count && v < frame.vertices.size(); ++v) {
                        const uint32_t c = frame.vertices[v].rgba;
                        a.r += c & 0xFF; a.g += (c >> 8) & 0xFF; a.b += (c >> 16) & 0xFF; a.a += c >> 24;
                        ++a.verts;
                    }
                }
                std::fprintf(r10, " ; draws %zu", agg.size());
                for (const auto& [h, a] : agg) {
                    const double n = a.verts ? a.verts : 1;
                    std::fprintf(r10, " %016llx:%u:%u:%.1f,%.1f,%.1f,%.1f:%016llx:%d", static_cast<unsigned long long>(h), a.draws,
                                 a.verts, a.r / n, a.g / n, a.b / n, a.a / n, static_cast<unsigned long long>(a.tex), a.modeled ? 1 : 0);
                }
            }
        }
        {   // GT4_R10_TEXCOUNT=FILE (N3 step 0, local): per frame, each memory texture's Draw commands on the list
            static FILE* tc = [] { const char* p = std::getenv("GT4_R10_TEXCOUNT"); return p ? std::fopen(p, "w") : nullptr; }();
            static unsigned long long listNo = 0;
            ++listNo;
            if (tc) {
                std::unordered_map<uint32_t, uint32_t> perTex;
                for (const auto& cmd : frame.commands)
                    if (cmd.kind == FrameList::Command::Kind::Draw && cmd.index < frame.draws.size() &&
                        frame.draws[cmd.index].texture.kind == TextureSource::Kind::Memory)
                        ++perTex[frame.draws[cmd.index].texture.id];
                for (const auto& [id, n] : perTex) std::fprintf(tc, "%llu %u %u\n", listNo, id, n);
                std::fflush(tc);
            }
        }
        {   // GT4_R10_DRAWLIST=VB1,VB2,LO,HI: the Draw commands at positions LO..HI (as GT4_GSN_LIMIT counts them)
            static unsigned long long l1 = 0, l2 = 0, lo = 0, hi = 0;
            static FILE* lf = nullptr;
            static bool lr = false;
            if (!lr) {
                lr = true;
                if (const char* e = std::getenv("GT4_R10_DRAWLIST"))
                    if (std::sscanf(e, "%llu,%llu,%llu,%llu", &l1, &l2, &lo, &hi) == 4) {
                        const char* p = std::getenv("GT4_R10_LOG");
                        std::string path = std::string(p ? p : "r10.log") + ".draws";
                        lf = std::fopen(path.c_str(), "w");
                    }
            }
            // (N4 notes: VB1..VB2 a range; only frames with a bracketed draw, the first three; the targets listed after)
            static int bracketFrames = 0;
            // the flare bracket's command span from the scene frame's markers (tagged or not)
            std::vector<uint8_t> inFlare(frame.commands.size() + 1, 0);
            if (frame.scene) {
                const auto& mk = frame.scene->markers;
                const auto& mc = frame.scene->markerCommands;
                uint32_t open = UINT32_MAX;
                for (size_t m = 0; m < mk.size() && m < mc.size(); ++m) {
                    if (mk[m].marker == gfx::scene::kMarkFlareBegin) open = mc[m];
                    if (mk[m].marker == gfx::scene::kMarkFlareEnd && open != UINT32_MAX) {
                        for (uint32_t k = open; k < mc[m] && k < inFlare.size(); ++k) inFlare[k] = 1;
                        open = UINT32_MAX;
                    }
                }
            }
            bool anyBracket = false;
            for (size_t k = 0; k < frame.commands.size(); ++k)
                anyBracket = anyBracket || (inFlare[k] && frame.commands[k].kind == FrameList::Command::Kind::Draw);
            if (lf && vblank >= l1 && vblank <= l2 && anyBracket && bracketFrames < 3) {
                ++bracketFrames;
                for (const Target& t : targets)
                    std::fprintf(lf, "%llu target %u bp %u bw %u psm %u height %u depth %d\n", static_cast<unsigned long long>(vblank),
                                 t.id, t.bp, t.bw, t.psm, t.height, t.depth ? 1 : 0);
                std::fprintf(lf, "%llu displayed %u targets %u %u\n", static_cast<unsigned long long>(vblank), frame.displayed ? 1u : 0u,
                             frame.display.target1, frame.display.target2);
                unsigned long long n = 0;
                for (size_t ci = 0; ci < frame.commands.size(); ++ci) {
                    const auto& cmd = frame.commands[ci];
                    if (cmd.kind != FrameList::Command::Kind::Draw) continue;
                    ++n;
                    if (inFlare[ci]) std::fprintf(lf, "%llu   in flare: draw %llu\n", static_cast<unsigned long long>(vblank), n);
                    if (n < lo || n > hi || cmd.index >= frame.draws.size()) continue;
                    const Draw& d = frame.draws[cmd.index];
                    const auto& c = d.state.c;
                    double r = 0, g = 0, b = 0, a = 0;
                    uint32_t mr = 0, mg = 0, mb = 0, ma = 0, nv = 0;
                    for (uint32_t v = d.first; v < d.first + d.count && v < frame.vertices.size() && !d.modeled; ++v) {
                        const uint32_t q = frame.vertices[v].rgba;
                        const uint32_t cr = q & 0xFF, cg = (q >> 8) & 0xFF, cb = (q >> 16) & 0xFF, ca = q >> 24;
                        r += cr; g += cg; b += cb; a += ca; ++nv;
                        mr = std::max(mr, cr); mg = std::max(mg, cg); mb = std::max(mb, cb); ma = std::max(ma, ca);
                    }
                    const double k = nv ? nv : 1;
                    std::fprintf(lf, "%llu draw %llu modeled %d prim %03x blend %u%u%u%u fix %u atst %u/%u/%u ztst %u/%u zw %u "
                                     "tex %u:%u:%u target %u count %u rgba %.1f,%.1f,%.1f,%.1f max %u,%u,%u,%u br %u\n",
                                 static_cast<unsigned long long>(vblank), n, d.modeled ? 1 : 0, d.state.prim,
                                 c.blendA, c.blendB, c.blendC, c.blendD, c.blendFixed, c.alphaTest, c.alphaFunc, c.alphaRef,
                                 c.depthTest, c.depthFunc, c.depthWrite, unsigned(d.texture.kind), d.texture.id,
                                 d.texture.version, d.target, d.count, r / k, g / k, b / k, a / k, mr, mg, mb, ma, unsigned(d.bracket));
                    if (d.modeled) {   // its model jobs: index in the frame (= the stream's batch run), renderer, body
                        std::fprintf(lf, "%llu   jobs of draw %llu (first %u):", static_cast<unsigned long long>(vblank), n, d.first);
                        for (size_t j = 0; j < frame.models.size(); ++j) {
                            const uint64_t f = frame.models[j].first;   // both in the frame's model vertex space
                            if (f >= d.first && f < uint64_t(d.first) + d.count) {
                                std::fprintf(lf, " %zu:r%u:b%04x:n%u:t%u/%u", j, frame.models[j].renderer,
                                             frame.models[j].chain >> 16, frame.models[j].count,
                                             unsigned(frame.models[j].pad[0]), unsigned(frame.models[j].pad[1]));
                                // its inputs, for a qword-by-qword compare: vblank, job, draw, the job, its words
                                static FILE* wf = [] {
                                    const char* p = std::getenv("GT4_R10_LOG");
                                    return std::fopen((std::string(p ? p : "r10.log") + ".words").c_str(), "wb");
                                }();
                                const gfx::ModelJob& mj = frame.models[j];
                                if (wf && mj.data + gfx::kModelWords <= frame.modelData.size()) {
                                    const uint64_t hdr[3] = {static_cast<uint64_t>(vblank), j, n};
                                    std::fwrite(hdr, sizeof hdr, 1, wf);
                                    std::fwrite(&mj, sizeof mj, 1, wf);
                                    std::fwrite(&frame.modelData[mj.data], 4, gfx::kModelWords, wf);
                                    std::fflush(wf);
                                }
                            }
                        }
                        std::fputc('\n', lf);
                    }
                }
                std::fflush(lf);
            }
        }
        r10LastVb = vblank;
        std::fprintf(r10, " ; lanes");
        for (auto& lane : r10Lanes) {
            if (lane.empty()) { std::fprintf(r10, " nan"); continue; }
            std::nth_element(lane.begin(), lane.begin() + lane.size() / 2, lane.end());
            std::fprintf(r10, " %.6g", lane[lane.size() / 2]);
        }
        std::fprintf(r10, " ; lights %zu", frame.lights.size());
        for (const auto& l : frame.lights)
            std::fprintf(r10, " | %ux%u fog %06x %d", l.width, l.height, l.fogColour & 0xFFFFFF, l.fogged ? 1 : 0);
        std::fputc('\n', r10);
        std::fflush(r10);
    }
    r10Constants.clear();
    for (auto& lane : r10Lanes) lane.clear();
    packetsThisFrame = 0;
    lastModelView = 0;
    if (sceneOpen) sceneVsyncInFrame = true;   // (its markers' indices span two lists: not checked)
    for (ViewBinding& v : viewBindings) {   // (as cut(): a view with tagged draws in this list and still open is cut, B6)
        if (sceneOpen && v.inList && !v.ended) v.cut = true;
        v.inList = false;
    }
    scenes.clear();
    // The texture pack switched on or off: every texture decodes again, with or without its replacement.
    const bool wanted = replacementsWanted.load() && texturePack && texturePack->isOpen();
    if (wanted != replacementsOn) {
        replacementsOn = wanted;
        resetTextures();
    }
    // Textures unused for 30 seconds are dropped, and the least recently used ones beyond a budget (GT4's
    // menus animate palettes: every palette is a texture, and they piled up until the GPU ran out of
    // memory). Two seconds, before, dropped a race's trackside textures between sightings: 300-600 decoded
    // and uploaded again every 5 s, in bursts of up to 333 a frame (the hitches), with the cache at 10-18 of
    // its 64 Mtexels.
    constexpr uint64_t kBudget = 64ull << 20;   // texels (256 MiB of RGBA8)
    constexpr uint64_t kUnused = 1800;          // frames
    if (!(++frameCount % 16) || cachedTexels > kBudget) {
        for (auto it = replacementTextures.begin(); it != replacementTextures.end();) {
            if (it->second.lastUsed + 120 < frameCount) {   // (a texture pack's images: large, and outside the budget)
                frame.retiredTextures.push_back(it->second.id);
                retireTextureIdentity(it->second.id);
                cachedTexels -= it->second.texels;
                it = replacementTextures.erase(it);
            } else {
                ++it;
            }
        }
        // (the per-address entries own no texture: they go with their last use)
        for (auto it = textureCache.begin(); it != textureCache.end();)
            it = it->second.lastUsed + kUnused < frameCount ? textureCache.erase(it) : std::next(it);
        for (auto it = decoded.begin(); it != decoded.end();) {
            if (it->second.lastUsed + kUnused < frameCount) {
                frame.retiredTextures.push_back(it->second.id);
                retireTextureIdentity(it->second.id);
                cachedTexels -= it->second.texels;
                it = decoded.erase(it);
            } else {
                ++it;
            }
        }
        if (cachedTexels > kBudget) {
            std::vector<std::pair<uint64_t, uint64_t>> byAge;   // last use, content key
            for (const auto& [key, d] : decoded) byAge.emplace_back(d.lastUsed, key);
            std::sort(byAge.begin(), byAge.end());
            for (const auto& [used, key] : byAge) {
                if (cachedTexels <= kBudget / 2 || used + 1 >= frameCount) break;
                auto it = decoded.find(key);
                frame.retiredTextures.push_back(it->second.id);
                retireTextureIdentity(it->second.id);
                cachedTexels -= it->second.texels;
                decoded.erase(it);
            }
        }
    }
    lastReinterpretation = {};
    lastMemoryReinterpretation = {};
    lastTexture.valid = false;
    frame.vblank = vblank;
    frame.displayed = true;
    frame.targets = targets;
    Display& d = frame.display;
    d.record = shown;
    // the target at the circuit's buffer: of its format's layout (GT4's 480p frames are 16-bit where the 480i ones
    // were 32-bit, at the same address), else the one written last
    auto displayed = [&](const gfx::DisplayCircuit& c) -> uint32_t {
        uint32_t best = 0;
        uint64_t bestWritten = 0;
        bool bestLayout = false;
        for (const SurfaceKey::Kind kind : {SurfaceKey::kColour32, SurfaceKey::kColourOther}) {
            const uint32_t id = surfaceTarget({c.base, c.width, kind});
            if (!id) continue;
            const Target& t = targets[id - 1];
            const bool layout = layout32(t.psm) == layout32(c.format);
            const uint64_t written = t.id < targetWritten.size() ? targetWritten[t.id] : 0;
            if (!best || (layout && !bestLayout) || (layout == bestLayout && written > bestWritten)) {
                best = t.id;
                bestWritten = written;
                bestLayout = layout;
            }
        }
        return best;
    };
    d.target1 = displayed(shown.circuit[0]);
    d.target2 = displayed(shown.circuit[1]);
    for (Target& t : targets)
        if (t.id == d.target1 || t.id == d.target2) t.lastUsed = frameCount;
    for (const uint32_t id : {d.target1, d.target2}) {   // (N4: the buffers shown lately are the picture; bracketFor)
        if (!id) continue;
        size_t slot = 0;
        for (size_t i = 0; i < std::size(shownTargets); ++i) {
            if (shownTargets[i].id == id) {
                slot = i;
                break;
            }
            if (shownTargets[i].at < shownTargets[slot].at) slot = i;
        }
        shownTargets[slot] = {id, frameCount};
    }
    clearSceneJobs();   // (B6: as at a cut)
    sceneViewsFrom = 0;
    std::swap(out, frame);
    frame.clear();
}

}  // namespace gt4::gsn
