// The native renderer's front end (see gs_frontend.h). Register layouts from the public GS documentation.
#include "gs_frontend.h"
#include "../gs/cpu_profile.h"
#include "texture_name.h"

#include <algorithm>
#include <cmath>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

}  // namespace

void FrameList::clear() {
    vertices.clear();
    draws.clear();
    commands.clear();
    lights.clear();
    textures.clear();
    reinterpretations.clear();
    retiredTextures.clear();
    resetTextures = false;
    targets.clear();
    displayed = false;
    dump = false;
}

void carryOver(FrameList& dropped, FrameList& into) {
    into.dump = into.dump || dropped.dump;
    if (!dropped.textures.empty()) {
        for (auto& t : into.textures) dropped.textures.push_back(std::move(t));   // the later version wins
        into.textures = std::move(dropped.textures);
    }
    into.retiredTextures.insert(into.retiredTextures.end(), dropped.retiredTextures.begin(), dropped.retiredTextures.end());
    into.resetTextures = into.resetTextures || dropped.resetTextures;
    dropped.textures.clear();
    dropped.retiredTextures.clear();
}

GsFrontend::GsFrontend() : pages(512), pageGeneration(512, 0) {}

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
        // GT4 moves XYOFFSET by a quarter pixel every other frame (its field jitter); drawn at a higher
        // resolution that shakes the picture, so offsets keep half-pixel steps (GT4_GSN_JITTER=keep: as is).
        static const bool keepJitter = [] { const char* e = std::getenv("GT4_GSN_JITTER"); return e && !std::strcmp(e, "keep"); }();
        const gfx::Context& c = contexts[(prim & gfx::kContext2) ? 1 : 0];
        const uint32_t jitter = keepJitter ? 0xFFFFu : 0xFFF8u;
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
    readStarted(t);
}

// A GS -> host read starts: the packed pixels from the memory model, or from the GPU where a draw made them.
void GsFrontend::readStarted(const GsMemory::Transfer& t) {
    {
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
            // GT4's reads (the car thumbnails it renders into 16-bit targets and keeps in EE memory) are of
            // one target in its own layout: those pixels come from the GPU. Anything else stays the memory
            // model's (stale where drawn).
            const Target& o = targets[owner - 1];
            if (oneOwner && !o.depth && o.bp == t.sbp && o.bw == t.sbw && o.psm == t.spsm &&
                (t.spsm == PSMCT32 || t.spsm == PSMCT24 || t.spsm == PSMCT16 || t.spsm == PSMCT16S)) {
                gpuRead = {owner, t.ssax, t.ssay, t.rrw, t.rrh, t.spsm};
                gpuReadPending = true;
            } else {
                static bool warned = false;
                if (!warned) {
                    warned = true;
                    std::fprintf(stderr, "gsn: a GS -> host read of drawn pixels in another layout (src %X/%u psm %X, target bp %X/%u "
                                 "psm %X) is served from the memory model, without what was drawn\n", t.sbp, t.sbw, t.spsm, o.bp, o.bw, o.psm);
                }
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

void GsFrontend::cut(FrameList& out) {
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
        ++pageGeneration[page];
    }
    ++uploads;
}

void GsFrontend::imageWritten(const uint8_t* bytes, size_t size) {
    if (!mem.write(bytes, size)) return;
    static const bool logChanges = std::getenv("GT4_GSN_XFER") != nullptr;
    if (logChanges) std::printf("[gsn changed] %zu bytes, %zu pages\n", size, uploadPages.size());
    for (const uint32_t page : uploadPages) ++pageGeneration[page];
    ++uploads;
}

void GsFrontend::memoryWritten(uint32_t bp, uint32_t bw, uint32_t psm, uint32_t x, uint32_t y, uint32_t w, uint32_t h, bool changed) {
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
    DrawState s = currentState(prim);
    // Flat shading: the last vertex's colour for the whole primitive.
    Vertex out[3];
    for (uint32_t k = 0; k < n; ++k) {
        out[k] = v[k];
        if (!((prim >> 3) & 1)) out[k].rgba = v[n - 1].rgba;
    }
    // Targets: the frame buffer and, when the depth test is on, the Z buffer.
    float x0 = out[0].x, x1 = out[0].x, y0 = out[0].y, y1 = out[0].y;
    for (uint32_t k = 1; k < n; ++k) {
        x0 = std::min(x0, out[k].x);
        x1 = std::max(x1, out[k].x);
        y0 = std::min(y0, out[k].y);
        y1 = std::max(y1, out[k].y);
    }
    const uint32_t scissorX0 = s.c.scissorX0, scissorX1 = s.c.scissorX1;
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
    const uint32_t colour = targetFor(s.c.colorBase, s.c.colorFormat, false, scissorY1 + 1, width);
    // ZTE with ZTST = ALWAYS and ZMSK set (the GS wants ZTE on: games "switch Z off" this way) neither tests nor
    // writes Z: no Z buffer (GT4's composition into the field buffer named the 448-line scene's).
    const bool zte = s.c.depthTest && !(s.c.depthFunc == 1 && !s.c.depthWrite);
    const uint32_t depth = zte ? targetFor(s.c.depthBase, s.c.depthFormat, true, scissorY1 + 1, width) : 0;
    if (!colour || (zte && !depth)) return;   // past the target limit
    if (s.c.colorKeep != 0xFFFFFFFFu) markWritten(colour, s.c.colorBase, width, s.c.colorFormat, left, right, top, bottom);
    if (depth && s.c.depthWrite) markWritten(depth, s.c.depthBase, width, s.c.depthFormat, left, right, top, bottom);
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
    // Batch with the previous draw when nothing changed.
    if (!frame.commands.empty() && frame.commands.back().kind == FrameList::Command::Kind::Draw) {
        Draw& last = frame.draws[frame.commands.back().index];
        if (last.topology == topology && last.state == s && last.target == colour && last.depth == depth &&
            last.texture.kind == texture.kind && last.texture.id == texture.id && last.texture.version == texture.version &&
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
    d.texture = texture;
    frame.vertices.insert(frame.vertices.end(), out, out + n);
    frame.commands.push_back({FrameList::Command::Kind::Draw, static_cast<uint32_t>(frame.draws.size())});
    frame.draws.push_back(d);
    ++stats.draws;
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

uint32_t GsFrontend::targetFor(uint32_t bp, uint32_t psm, bool depth, uint32_t heightNeeded, uint32_t bw) {
    const bool wide = !depth && layout32(psm);
    for (Target& t : targets) {
        if (t.depth != depth || t.bp != bp) continue;
        if (t.bw != bw) continue;
        if (!depth && layout32(t.psm) != wide) continue;
        if (depth && (t.psm & 2) != (psm & 2)) continue;
        if (heightNeeded > t.height) t.height = (heightNeeded + 31) & ~31u;
        if (!depth) t.psm = psm;   // CT24 and CT32 share the layout; the last write tells which alpha is valid
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
        // Forget what the old buffer owned: its pages are plain memory again.
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
    t.height = (std::max<uint32_t>(heightNeeded, 32) + 31) & ~31u;
    if (slot) *slot = t;
    else targets.push_back(t);
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

TextureSource GsFrontend::textureFor(const DrawState& s) {
    GT4_CPU_SCOPE("front end: texture lookup");
    const GsMemory::Tex0 t = GsMemory::Tex0::of(s.c.texture);
    const uint32_t w = 1u << std::min<uint32_t>(t.tw, 10), h = 1u << std::min<uint32_t>(t.th, 10);
    uint32_t pw, ph;
    pageSize(t.psm, pw, ph);
    const uint32_t perRow = std::max<uint32_t>(1, (t.psm == PSMT8 || t.psm == PSMT4) ? t.tbw / 2 : t.tbw);
    // The most recent writer of the pages the texture covers.
    uint32_t owner = 0;
    uint64_t newest = 0, generation = 0, content = 0;
    const uint32_t rows = (h + ph - 1) / ph, columns = std::min<uint32_t>(perRow, (w + pw - 1) / pw);
    for (uint32_t py = 0; py < rows; ++py)
        for (uint32_t px = 0; px < std::max<uint32_t>(columns, 1); ++px)
            for (uint32_t extra = 0; extra < 2; ++extra) {
                if (extra && !(t.tbp % 32)) break;
                const uint32_t page = (t.tbp / 32 + py * perRow + px + extra) & 511;
                generation += pageGeneration[page] * (page + 1);
                content = mix(content, page);
                if (pages[page].target && pages[page].serial > newest) {
                    newest = pages[page].serial;
                    owner = pages[page].target;
                }
            }
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
        for (const Target& candidate : targets) {
            if (candidate.depth || candidate.bp != t.tbp || candidate.bw != t.tbw) continue;
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
        src.reinterpreted = true;
        ++stats.reinterpreted;
    }
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
            ++stats.reused;
            src.kind = TextureSource::Kind::Memory;
            src.id = c.id;
            src.version = 1;
            return src;
        }
        d.id = c.id = nextTexture++;
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
        for (uint32_t i = 0; i < levelCount; ++i) {
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

void GsFrontend::vertices(const InputVertex* in, size_t count, bool withFog) {
    const uint32_t prim = primValue();
    // Triangle strips, not UV texturing (FST): the renderers' runs. Others vertex by vertex.
    if ((prim & 7) != 4 || ((prim >> 8) & 1) || count < 3) {
        for (size_t k = 0; k < count; ++k) vertex(in[k], withFog);
        return;
    }
    const DrawState s = currentState(prim);
    const gfx::Context& c = contexts[(prim & gfx::kContext2) ? 1 : 0];
    static const bool keepJitter = [] { const char* e = std::getenv("GT4_GSN_JITTER"); return e && !std::strcmp(e, "keep"); }();
    const uint32_t jitter = keepJitter ? 0xFFFFu : 0xFFF8u;
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
            float x0 = t[0].x, x1 = t[0].x, y0 = t[0].y, y1 = t[0].y;
            for (int j = 1; j < 3; ++j) {
                x0 = std::min(x0, t[j].x); x1 = std::max(x1, t[j].x); y0 = std::min(y0, t[j].y); y1 = std::max(y1, t[j].y);
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
                    last.first + last.count == frame.vertices.size())
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

void GsFrontend::resetTextures() {
    lastTexture.valid = false;
    replacementTextures.clear();
    textureCache.clear();
    decoded.clear();
    cachedTexels = 0;
    frame.textures.clear();
    frame.resetTextures = true;
}

void GsFrontend::vsync(uint64_t vblank, FrameList& out) {
    packetsThisFrame = 0;
    scenes.clear();
    // The texture pack switched on or off: every texture decodes again, with or without its replacement.
    const bool wanted = replacementsWanted.load() && texturePack && texturePack->isOpen();
    if (wanted != replacementsOn) {
        replacementsOn = wanted;
        resetTextures();
    }
    // Textures unused for two seconds are dropped, and the least recently used ones beyond a budget (GT4's
    // menus animate palettes: every palette is a texture, and they piled up until the GPU ran out of
    // memory).
    constexpr uint64_t kBudget = 64ull << 20;   // texels (256 MiB of RGBA8)
    if (!(++frameCount % 16) || cachedTexels > kBudget) {
        for (auto it = replacementTextures.begin(); it != replacementTextures.end();) {
            if (it->second.lastUsed + 120 < frameCount) {
                frame.retiredTextures.push_back(it->second.id);
                cachedTexels -= it->second.texels;
                it = replacementTextures.erase(it);
            } else {
                ++it;
            }
        }
        // (the per-address entries own no texture: they go with their last use)
        for (auto it = textureCache.begin(); it != textureCache.end();)
            it = it->second.lastUsed + 120 < frameCount ? textureCache.erase(it) : std::next(it);
        for (auto it = decoded.begin(); it != decoded.end();) {
            if (it->second.lastUsed + 120 < frameCount) {
                frame.retiredTextures.push_back(it->second.id);
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
    auto displayed = [&](const gfx::DisplayCircuit& c) -> uint32_t {
        for (const Target& t : targets)
            if (!t.depth && t.bp == c.base && t.bw == c.width) return t.id;
        return 0;
    };
    d.target1 = displayed(shown.circuit[0]);
    d.target2 = displayed(shown.circuit[1]);
    for (Target& t : targets)
        if (t.id == d.target1 || t.id == d.target2) t.lastUsed = frameCount;
    std::swap(out, frame);
    frame.clear();
}

}  // namespace gt4::gsn
