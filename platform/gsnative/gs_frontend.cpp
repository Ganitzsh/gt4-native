// The native renderer's front end (see gs_frontend.h). Register layouts from the public GS documentation.
#include "gs_frontend.h"

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
    textures.clear();
    reinterpretations.clear();
    retiredTextures.clear();
    resetTextures = false;
    targets.clear();
}

void carryOver(FrameList& dropped, FrameList& into) {
    if (!dropped.textures.empty()) {
        for (auto& t : into.textures) dropped.textures.push_back(std::move(t));   // the later version wins
        into.textures = std::move(dropped.textures);
    }
    into.retiredTextures.insert(into.retiredTextures.end(), dropped.retiredTextures.begin(), dropped.retiredTextures.end());
    into.resetTextures = into.resetTextures || dropped.resetTextures;
    dropped.textures.clear();
    dropped.retiredTextures.clear();
}

GsFrontend::GsFrontend() : pages(512), pageGeneration(512, 0) {
    reg[PRMODECONT] = 1;
    reg[COLCLAMP] = 1;
}

uint32_t GsFrontend::primValue() const {
    // With PRMODECONT = 0 the attributes (everything but the type) come from PRMODE.
    const uint64_t p = reg[PRIM];
    if (reg[PRMODECONT] & 1) return static_cast<uint32_t>(p & 0x7FF);
    return static_cast<uint32_t>((p & 7) | (reg[PRMODE] & 0x7F8));
}

DrawState GsFrontend::currentState(uint32_t prim) const {
    const uint32_t c = (prim >> 9) & 1;
    DrawState s;
    s.prim = prim;
    s.frame = reg[FRAME_1 + c];
    s.zbuf = reg[ZBUF_1 + c];
    s.tex0 = reg[TEX0_1 + c];
    s.tex1 = reg[TEX1_1 + c];
    s.clamp = reg[CLAMP_1 + c];
    s.test = reg[TEST_1 + c];
    s.alpha = reg[ALPHA_1 + c];
    s.scissor = reg[SCISSOR_1 + c];
    s.texa = reg[TEXA];
    // Mipmapping: MXL levels beyond the first with a MMIN that uses them.
    if (((s.tex1 >> 2) & 7) && ((s.tex1 >> 6) & 7) >= 2) {
        s.miptbp1 = reg[MIPTBP1_1 + c];
        s.miptbp2 = reg[MIPTBP2_1 + c];
    }
    s.fba = static_cast<uint32_t>(reg[FBA_1 + c] & 1);
    s.pabe = static_cast<uint32_t>(reg[PABE] & 1);
    s.dthe = static_cast<uint32_t>(reg[DTHE] & 1);
    s.colclamp = static_cast<uint32_t>(reg[COLCLAMP] & 1);
    s.fogcol = static_cast<uint32_t>(reg[FOGCOL] & 0xFFFFFF);
    // Fields that do not matter for the draw are cleared, so that equal draws batch.
    if (!((prim >> 4) & 1)) s.tex0 = s.tex1 = s.clamp = s.texa = s.miptbp1 = s.miptbp2 = 0;
    if (!((prim >> 6) & 1)) s.alpha = 0;
    if (!((prim >> 5) & 1)) s.fogcol = 0;
    return s;
}

void GsFrontend::write(uint32_t r, uint64_t value) {
    r &= 0x7F;
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
        const uint64_t offset = reg[XYOFFSET_1 + ((prim >> 9) & 1)];
        const uint32_t x = static_cast<uint32_t>(value & 0xFFFF), y = static_cast<uint32_t>((value >> 16) & 0xFFFF);
        v.x = (static_cast<float>(x) - static_cast<float>(offset & 0xFFFF)) / 16.0f;
        v.y = (static_cast<float>(y) - static_cast<float>((offset >> 32) & 0xFFFF)) / 16.0f;
        const bool f = r == XYZF2 || r == XYZF3;
        v.z = f ? static_cast<uint32_t>((value >> 32) & 0xFFFFFF) : static_cast<uint32_t>(value >> 32);
        v.fog = f ? static_cast<uint32_t>(value >> 56) : static_cast<uint32_t>(reg[FOG] >> 56);
        v.rgba = static_cast<uint32_t>(reg[RGBAQ]);
        v.s = st[0];
        v.t = st[1];
        v.q = q;
        v.u = static_cast<float>(reg[UV] & 0x3FFF) / 16.0f;
        v.v = static_cast<float>((reg[UV] >> 16) & 0x3FFF) / 16.0f;
        kick(r == XYZF2 || r == XYZ2);
        return;
    }
    case TEX0_1: case TEX0_2:
        reg[r] = value;
        clutWritten(value);
        return;
    case TEX2_1: case TEX2_2: {
        // TEX2: PSM and the CLUT fields of TEX0.
        const uint64_t mask = (0x3Full << 20) | (~0ull << 37);
        uint64_t& tex0 = reg[TEX0_1 + (r - TEX2_1)];
        tex0 = (tex0 & ~mask) | (value & mask);
        clutWritten(tex0);
        return;
    }
    case TRXDIR:
        reg[r] = value;
        transferStarted(static_cast<uint32_t>(value & 3));
        return;
    case HWREG:
        if (imageActive) mem.write(reinterpret_cast<const uint8_t*>(&value), 8);
        return;
    default:
        reg[r] = value;
        return;
    }
}

// A TEX0/TEX2 write. A CLUT loaded from a page a 32-bit target owns holds what was drawn there, which only
// the GPU has: the reinterpretations of draws with this TEX0 read their palette from that target.
void GsFrontend::clutWritten(uint64_t tex0) {
    const GsMemory::Tex0 t = GsMemory::Tex0::decode(tex0);
    if (!mem.tex0Written(t)) return;
    static const bool logLoads = std::getenv("GT4_GSN_XFER") != nullptr;
    if (logLoads) {
        const uint16_t* c = reinterpret_cast<const uint16_t*>(mem.clutBuffer());
        std::printf("[gsn clut] packet %u tex0 %016llX cbp %X cpsm %X csm %u csa %u psm %X, entry 64 %04X%04X\n", packetsThisFrame - 1,
                    static_cast<unsigned long long>(tex0), t.cbp, t.cpsm, t.csm, t.csa, t.psm, c[256 + 64], c[64]);
    }
    clutSource = {};
    const PageOwner& page = pages[(t.cbp / 32) & 511];
    if (!page.target || (t.cpsm != PSMCT32 && t.cpsm != PSMCT24)) return;
    const Target& c = targets[page.target - 1];
    if (c.depth || !layout32(c.psm)) return;
    clutSource = {page.target, tex0 & kClutFields};
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
        memoryWritten(t.dbp, t.dbw, t.dpsm, t.dsax, t.dsay, t.rrw, t.rrh);
        ++stats.uploads;
    } else if (dir == 2) {
        mem.copy(t);   // a copy out of a render target is stale here (the target lives on the GPU)
        memoryWritten(t.dbp, t.dbw, t.dpsm, t.dsax, t.dsay, t.rrw, t.rrh);
        ++stats.copies;
    }
}

void GsFrontend::memoryWritten(uint32_t bp, uint32_t bw, uint32_t psm, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    uint32_t pw, ph;
    pageSize(psm, pw, ph);
    const uint32_t perRow = std::max<uint32_t>(1, (psm == PSMT8 || psm == PSMT4) ? bw / 2 : bw);
    for (uint32_t py = y / ph; py <= (y + std::max<uint32_t>(h, 1) - 1) / ph; ++py)
        for (uint32_t px = x / pw; px <= (x + std::max<uint32_t>(w, 1) - 1) / pw; ++px) {
            for (uint32_t extra = 0; extra < 2; ++extra) {   // a base that is not page-aligned spills into the next page
                const uint32_t page = (bp / 32 + py * perRow + px + extra) & 511;
                if (extra && !(bp % 32)) break;
                ++pageGeneration[page];
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
    const uint32_t scissorX0 = static_cast<uint32_t>(s.scissor & 0x7FF), scissorX1 = static_cast<uint32_t>((s.scissor >> 16) & 0x7FF);
    const uint32_t left = static_cast<uint32_t>(std::clamp(x0, static_cast<float>(scissorX0), static_cast<float>(scissorX1)));
    // The last pixel covered: an edge on a pixel boundary covers the pixel before it.
    const uint32_t right = static_cast<uint32_t>(std::clamp(std::max(x0, std::ceil(x1) - 1.0f), static_cast<float>(scissorX0), static_cast<float>(scissorX1)));
    const uint32_t scissorY0 = static_cast<uint32_t>((s.scissor >> 32) & 0x7FF), scissorY1 = static_cast<uint32_t>((s.scissor >> 48) & 0x7FF);
    const uint32_t top = static_cast<uint32_t>(std::clamp(y0, static_cast<float>(scissorY0), static_cast<float>(scissorY1)));
    const uint32_t bottom = static_cast<uint32_t>(std::clamp(std::max(y0, std::ceil(y1) - 1.0f), static_cast<float>(scissorY0), static_cast<float>(scissorY1)));
    const uint32_t width = static_cast<uint32_t>((s.frame >> 16) & 0x3F);
    const uint32_t colour = targetFor(s.frame, false, scissorY1 + 1, width);
    const bool zte = (s.test >> 16) & 1;
    const uint32_t depth = zte ? targetFor(s.zbuf, true, scissorY1 + 1, width) : 0;
    const uint32_t fbmsk = static_cast<uint32_t>(s.frame >> 32);
    if (fbmsk != 0xFFFFFFFFu) markWritten(colour, static_cast<uint32_t>(s.frame & 0x1FF) * 32, static_cast<uint32_t>((s.frame >> 16) & 0x3F),
                                          static_cast<uint32_t>((s.frame >> 24) & 0x3F), left, right, top, bottom);
    if (depth && !((s.zbuf >> 32) & 1))
        markWritten(depth, static_cast<uint32_t>(s.zbuf & 0x1FF) * 32, static_cast<uint32_t>((s.frame >> 16) & 0x3F),
                    static_cast<uint32_t>(((s.zbuf >> 24) & 0xF) | 0x30), left, right, top, bottom);
    const TextureSource texture = ((prim >> 4) & 1) ? textureFor(s) : TextureSource{};
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

uint32_t GsFrontend::targetFor(uint64_t frameReg, bool depth, uint32_t heightNeeded, uint32_t bw) {
    const uint32_t bp = static_cast<uint32_t>(frameReg & 0x1FF) * 32;
    const uint32_t psm = depth ? static_cast<uint32_t>(((frameReg >> 24) & 0xF) | 0x30) : static_cast<uint32_t>((frameReg >> 24) & 0x3F);
    const bool wide = !depth && layout32(psm);
    for (Target& t : targets) {
        if (t.depth != depth || t.bp != bp) continue;
        if (t.bw != bw) continue;
        if (!depth && layout32(t.psm) != wide) continue;
        if (depth && (t.psm & 2) != (psm & 2)) continue;
        if (heightNeeded > t.height) t.height = (heightNeeded + 31) & ~31u;
        if (!depth) t.psm = psm;   // CT24 and CT32 share the layout; the last write tells which alpha is valid
        return t.id;
    }
    Target t;
    t.id = static_cast<uint32_t>(targets.size()) + 1;
    t.bp = bp;
    t.bw = bw;
    t.psm = psm;
    t.depth = depth;
    t.height = (std::max<uint32_t>(heightNeeded, 32) + 31) & ~31u;
    targets.push_back(t);
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
        for (uint32_t px = first; px <= last; ++px) pages[(bp / 32 + py * perRow + px) & 511] = {targetId, ++serial};
    if (targetWritten.size() <= targetId) targetWritten.resize(targetId + 1, 0);
    targetWritten[targetId] = serial;
}

TextureSource GsFrontend::textureFor(const DrawState& s) {
    const GsMemory::Tex0 t = GsMemory::Tex0::decode(s.tex0);
    const uint32_t w = 1u << std::min<uint32_t>(t.tw, 10), h = 1u << std::min<uint32_t>(t.th, 10);
    uint32_t pw, ph;
    pageSize(t.psm, pw, ph);
    const uint32_t perRow = std::max<uint32_t>(1, (t.psm == PSMT8 || t.psm == PSMT4) ? t.tbw / 2 : t.tbw);
    // The most recent writer of the pages the texture covers.
    uint32_t owner = 0;
    uint64_t newest = 0, generation = 0;
    const uint32_t rows = (h + ph - 1) / ph, columns = std::min<uint32_t>(perRow, (w + pw - 1) / pw);
    for (uint32_t py = 0; py < rows; ++py)
        for (uint32_t px = 0; px < std::max<uint32_t>(columns, 1); ++px)
            for (uint32_t extra = 0; extra < 2; ++extra) {
                if (extra && !(t.tbp % 32)) break;
                const uint32_t page = (t.tbp / 32 + py * perRow + px + extra) & 511;
                generation += pageGeneration[page] * (page + 1);
                if (pages[page].target && pages[page].serial > newest) {
                    newest = pages[page].serial;
                    owner = pages[page].target;
                }
            }
    // A target that starts at the texture's own base page wins: textures are declared at powers of two, so
    // their extent often runs past the buffer they read into later targets.
    // Several targets can start at one page (GT4 draws a 128-wide environment map and its 640-wide scene at
    // the same address): the most recently written one of the texture's width.
    if (!(t.tbp % 32)) {
        uint64_t best = 0;
        for (const Target& candidate : targets) {
            if (candidate.depth || candidate.bp != t.tbp || candidate.bw != t.tbw) continue;
            const uint64_t written = candidate.id < targetWritten.size() ? targetWritten[candidate.id] : 0;
            if (written && written >= best) {
                best = written;
                owner = candidate.id;
            }
        }
    }
    TextureSource src;
    if (owner) {
        const Target& target = targets[owner - 1];
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
            r.tex0 = s.tex0;
            r.texa = s.texa;
            r.width = w;
            r.height = h;
            std::memcpy(r.clut.data(), mem.clutBuffer(), r.clut.size());
            if (clutSource.target && clutSource.tex0 == (s.tex0 & kClutFields)) {
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
                if (p.target == r.target && p.tex0 == r.tex0 && p.texa == r.texa && p.clut == r.clut && p.clutTarget == r.clutTarget &&
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
    if (indexed && clutSource.target && clutSource.tex0 == (s.tex0 & kClutFields)) {
        const Target& c = targets[clutSource.target - 1];
        Reinterpretation r;
        r.tex0 = s.tex0;
        r.texa = s.texa;
        r.width = w;
        r.height = h;
        r.indices.resize(size_t(w) * h);
        mem.indices(t, w, h, r.indices.data());
        std::memcpy(r.clut.data(), mem.clutBuffer(), r.clut.size());
        r.clutTarget = c.id;
        r.clutTargetBp = c.bp;
        r.clutTargetBw = c.bw;
        src.kind = TextureSource::Kind::Reinterpreted;
        src.id = static_cast<uint32_t>(frame.reinterpretations.size());
        frame.reinterpretations.push_back(std::move(r));
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
    if (s.miptbp1 || s.miptbp2 || ((s.tex1 >> 9) & 1)) {
        const uint32_t mxl = std::min<uint32_t>((s.tex1 >> 2) & 7, 6);
        for (uint32_t i = 1; i <= mxl; ++i) {
            const Level& prev = levels[i - 1];
            if (prev.w == 1 && prev.h == 1) break;
            Level& l = levels[i];
            l.w = std::max<uint32_t>(prev.w / 2, 1);
            l.h = std::max<uint32_t>(prev.h / 2, 1);
            if (((s.tex1 >> 9) & 1) && i <= 3) {   // MTBA
                l.tbp = prev.tbp + prev.w * prev.h * psmBits(t.psm) / 8 / 256;
                l.tbw = std::max<uint32_t>(prev.tbw / 2, 1);
            } else {
                const uint64_t m = i <= 3 ? s.miptbp1 : s.miptbp2;
                const uint32_t k = (i - 1) % 3;
                l.tbp = static_cast<uint32_t>((m >> (20 * k)) & 0x3FFF);
                l.tbw = static_cast<uint32_t>((m >> (20 * k + 14)) & 0x3F);
            }
            const uint32_t lrows = (l.h + ph - 1) / ph, lperRow = std::max<uint32_t>(1, (t.psm == PSMT8 || t.psm == PSMT4) ? l.tbw / 2 : l.tbw);
            for (uint32_t py = 0; py < lrows; ++py)
                for (uint32_t px = 0; px < std::max<uint32_t>(1, std::min<uint32_t>(lperRow, (l.w + pw - 1) / pw)); ++px)
                    generation += pageGeneration[(l.tbp / 32 + py * lperRow + px) & 511] * (i * 512 + py * lperRow + px + 1);
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
        key = mix(key, s.texa);
    }
    Cached& c = textureCache[key];
    if (!c.id) c.id = nextTexture++;
    c.lastUsed = frameCount;
    if (!c.version || c.generation != generation || c.clutHash != clutHash) {
        // The back end uploads a frame's textures before its draws: content that changes within a frame
        // needs a new id, the earlier draws keeping the old one.
        if (c.version && c.decodedFrame == frameCount) {
            frame.retiredTextures.push_back(c.id);
            c.id = nextTexture++;
        }
        c.decodedFrame = frameCount;
        ++c.version;
        c.generation = generation;
        c.clutHash = clutHash;
        TextureImage image;
        image.id = c.id;
        image.version = c.version;
        image.width = w;
        image.height = h;
        image.levels = levelCount;
        size_t texels = 0;
        for (uint32_t i = 0; i < levelCount; ++i) texels += size_t(levels[i].w) * levels[i].h;
        image.pixels.resize(texels);
        Texa texa;
        texa.ta0 = static_cast<uint8_t>(s.texa & 0xFF);
        texa.aem = (s.texa >> 15) & 1;
        texa.ta1 = static_cast<uint8_t>((s.texa >> 32) & 0xFF);
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
    src.kind = TextureSource::Kind::Memory;
    src.id = c.id;
    src.version = c.version;
    return src;
}

void GsFrontend::privileged(uint32_t offset, uint64_t value) {
    if (offset < 0x100 * 16 && !(offset & 0xF)) priv[offset >> 4] = value;
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
            if (imageActive) mem.write(data + q16 * 16, n * 16);
            static const bool logImages = std::getenv("GT4_GSN_XFER") != nullptr;
            if (logImages) std::printf("[gsn image] path %u: %zu of %u qwords%s\n", path, n, g.loops, imageActive ? "" : " (no transfer)");
            q16 += n;
            g.loops -= static_cast<uint32_t>(n);
        }
    }
}

void GsFrontend::resetTextures() {
    textureCache.clear();
    frame.textures.clear();
    frame.resetTextures = true;
}

void GsFrontend::vsync(uint64_t vblank, FrameList& out) {
    packetsThisFrame = 0;
    // Textures unused for ten seconds are dropped.
    if (!(++frameCount % 64))
        for (auto it = textureCache.begin(); it != textureCache.end();) {
            if (it->second.lastUsed + 600 < frameCount) {
                frame.retiredTextures.push_back(it->second.id);
                it = textureCache.erase(it);
            } else {
                ++it;
            }
        }
    lastReinterpretation = {};
    frame.vblank = vblank;
    frame.targets = targets;
    Display& d = frame.display;
    d.pmode = priv[0x00];
    d.smode2 = priv[0x02];
    d.dispfb1 = priv[0x07];
    d.display1 = priv[0x08];
    d.dispfb2 = priv[0x09];
    d.display2 = priv[0x0A];
    d.bgcolor = priv[0x0E];
    auto displayed = [&](uint64_t dispfb) -> uint32_t {
        const uint32_t bp = static_cast<uint32_t>(dispfb & 0x1FF) * 32, bw = static_cast<uint32_t>((dispfb >> 9) & 0x3F);
        for (const Target& t : targets)
            if (!t.depth && t.bp == bp && t.bw == bw) return t.id;
        return 0;
    };
    d.target1 = displayed(d.dispfb1);
    d.target2 = displayed(d.dispfb2);
    std::swap(out, frame);
    frame.clear();
}

}  // namespace gt4::gsn
