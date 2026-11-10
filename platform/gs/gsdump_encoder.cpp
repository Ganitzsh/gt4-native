// The front end's GS dump (gsdump_encoder.h, PLAN.md C12 step 3).
#include "gsdump_encoder.h"

#include "gs_privileged.h"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace gt4::gs {

namespace {

// GS register addresses (the public GS documentation).
enum : uint32_t {
    PRIM = 0x00, RGBAQ = 0x01, ST = 0x02, UV = 0x03, XYZF2 = 0x04, XYZ2 = 0x05, TEX0_1 = 0x06, CLAMP_1 = 0x08, FOG = 0x0A,
    XYZF3 = 0x0C, XYZ3 = 0x0D, AD = 0x0E, NOP = 0x0F, TEX1_1 = 0x14, TEX2_1 = 0x16, XYOFFSET_1 = 0x18, PRMODECONT = 0x1A,
    PRMODE = 0x1B, TEXCLUT = 0x1C, SCANMSK = 0x22, MIPTBP1_1 = 0x34, MIPTBP2_1 = 0x36, TEXA = 0x3B, FOGCOL = 0x3D,
    TEXFLUSH = 0x3F, SCISSOR_1 = 0x40, ALPHA_1 = 0x42, DIMX = 0x44, DTHE = 0x45, COLCLAMP = 0x46, TEST_1 = 0x47,
    PABE = 0x49, FBA_1 = 0x4A, FRAME_1 = 0x4C, ZBUF_1 = 0x4E, BITBLTBUF = 0x50, TRXPOS = 0x51, TRXREG = 0x52,
    TRXDIR = 0x53,
};

uint64_t textureValue(const gfx::Texture& t) {
    return uint64_t(t.base & 0x3FFF) | uint64_t(t.width & 0x3F) << 14 | uint64_t(t.format & 0x3F) << 20 |
           uint64_t(t.log2Width & 0xF) << 26 | uint64_t(t.log2Height & 0xF) << 30 | uint64_t(t.useAlpha & 1) << 34 |
           uint64_t(t.function & 3) << 35 | uint64_t(t.clutBase & 0x3FFF) << 37 | uint64_t(t.clutFormat & 0xF) << 51 |
           uint64_t(t.clutStorage & 1) << 55 | uint64_t(t.clutOffset & 0x1F) << 56 | uint64_t(t.clutLoad & 7) << 61;
}

// A context group's register value: the inverse of gfx.h's setContextGroup (TEX2 from the record's palette fields).
uint64_t contextValue(const gfx::Context& c, const gfx::Texture& palette, uint32_t group) {
    const gfx::Sampler& s = c.sampler;
    switch (group) {
    case gfx::kFrame:
        return uint64_t((c.colorBase / 32) & 0x1FF) | uint64_t(c.colorWidth & 0x3F) << 16 | uint64_t(c.colorFormat & 0x3F) << 24 |
               uint64_t(c.colorKeep) << 32;
    case gfx::kOrigin: return uint64_t(c.originX) | uint64_t(c.originY) << 32;
    case gfx::kForceAlpha: return c.forceAlpha & 1;
    case gfx::kDepthBuffer:
        return uint64_t((c.depthBase / 32) & 0x1FF) | uint64_t(c.depthFormat & 0xF) << 24 | uint64_t(c.depthWrite ? 0 : 1) << 32;
    case gfx::kScissor:
        return uint64_t(c.scissorX0 & 0x7FF) | uint64_t(c.scissorX1 & 0x7FF) << 16 | uint64_t(c.scissorY0 & 0x7FF) << 32 |
               uint64_t(c.scissorY1 & 0x7FF) << 48;
    case gfx::kBlendGroup:
        return uint64_t(c.blendA & 3) | uint64_t(c.blendB & 3) << 2 | uint64_t(c.blendC & 3) << 4 | uint64_t(c.blendD & 3) << 6 |
               uint64_t(c.blendFixed) << 32;
    case gfx::kTests:
        return uint64_t(c.alphaTest & 1) | uint64_t(c.alphaFunc & 7) << 1 | uint64_t(c.alphaRef) << 4 | uint64_t(c.alphaFail & 3) << 12 |
               uint64_t(c.destTest & 1) << 14 | uint64_t(c.destPass & 1) << 15 | uint64_t(c.depthTest & 1) << 16 |
               uint64_t(c.depthFunc & 3) << 17;
    case gfx::kWrap:
        return uint64_t(s.wrapU & 3) | uint64_t(s.wrapV & 3) << 2 | uint64_t(s.minU & 0x3FF) << 4 | uint64_t(s.maxU & 0x3FF) << 14 |
               uint64_t(s.minV & 0x3FF) << 24 | uint64_t(s.maxV & 0x3FF) << 34;
    case gfx::kPalette:
        return uint64_t(palette.format & 0x3F) << 20 | uint64_t(palette.clutBase & 0x3FFF) << 37 | uint64_t(palette.clutFormat & 0xF) << 51 |
               uint64_t(palette.clutStorage & 1) << 55 | uint64_t(palette.clutOffset & 0x1F) << 56 | uint64_t(palette.clutLoad & 7) << 61;
    case gfx::kFilter:
        return uint64_t(s.lodFixed & 1) | uint64_t(s.mipMax & 7) << 2 | uint64_t(s.magLinear & 1) << 5 | uint64_t(s.minFilter & 7) << 6 |
               uint64_t(s.mipAuto & 1) << 9 | uint64_t(s.lodL & 3) << 19 | uint64_t(uint16_t(s.lodK) & 0xFFF) << 32;
    case gfx::kTexture: return textureValue(c.texture);
    case gfx::kMip1: case gfx::kMip2: {
        uint64_t v = 0;
        for (uint32_t k = 0; k < 3; ++k) {
            const uint32_t level = (group == gfx::kMip1 ? 0 : 3) + k;
            v |= uint64_t(s.mipBase[level] & 0x3FFF) << (20 * k) | uint64_t(s.mipWidth[level] & 0x3F) << (20 * k + 14);
        }
        return v;
    }
    default: return 0;
    }
}

uint64_t sharedValue(const gfx::Shared& s, uint32_t group) {
    switch (group) {
    case gfx::kColorClamp: return s.colorClamp & 1;
    case gfx::kDitherMatrix: return s.ditherMatrix;
    case gfx::kDither: return s.dither & 1;
    case gfx::kFogColor: return s.fogColor & 0xFFFFFF;
    case gfx::kPerPixelBlend: return s.perPixelBlend & 1;
    case gfx::kAttributes: return s.attributesFromPrim & 1;
    case gfx::kScanMask: return s.scanMask & 3;
    case gfx::kTexAlpha: return uint64_t(s.texAlpha0) | uint64_t(s.texBlackTransparent & 1) << 15 | uint64_t(s.texAlpha1) << 32;
    case gfx::kClutLayout: return uint64_t(s.clutWidth & 0x3F) | uint64_t(s.clutU & 0x3F) << 6 | uint64_t(s.clutV & 0x3FF) << 12;
    case gfx::kTextureFlush: return 0;
    default: return 0;
    }
}

uint32_t floatBits(float f) { return std::bit_cast<uint32_t>(f); }

}  // namespace

bool GsDumpEncoder::parse(const char* value, Options& out) {
    if (!value || !*value) return false;
    out.from = 0;
    out.count = 1;
    std::string s(value);
    const size_t a = s.find(',');
    out.path = s.substr(0, a);
    if (out.path.empty()) return false;
    if (a != std::string::npos) {
        const size_t b = s.find(',', a + 1);
        out.from = std::strtoull(s.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1).c_str(), nullptr, 10);
        if (b != std::string::npos) out.count = std::max<uint64_t>(1, std::strtoull(s.c_str() + b + 1, nullptr, 10));
    }
    return true;
}

GsDumpEncoder::GsDumpEncoder(Options o) : options(std::move(o)) {}

GsDumpEncoder::~GsDumpEncoder() {
    std::string error;
    if (writer.isOpen()) writer.close(error);
}

void GsDumpEncoder::word(uint64_t v) {
    const size_t at = out.size();
    out.resize(at + 8);
    std::memcpy(out.data() + at, &v, 8);
}

void GsDumpEncoder::tag(uint32_t nloop, bool eop, uint32_t flg, uint32_t nreg, uint64_t regs, bool pre, uint32_t prim) {
    word(uint64_t(nloop & 0x7FFF) | uint64_t(eop) << 15 | uint64_t(pre) << 46 | uint64_t(prim & 0x7FF) << 47 | uint64_t(flg & 3) << 58 |
         uint64_t(nreg & 0xF) << 60);
    word(regs);
}

void GsDumpEncoder::put(uint32_t path) {
    if (out.empty()) return;
    if (writer.isOpen()) {
        writer.transfer(path, out.data(), out.size());
        ++counts.transfers;
        counts.bytes += out.size();
    }
    out.clear();
}

void GsDumpEncoder::apply(uint32_t reg, uint64_t value) {
    GsDumpState& s = registers;
    const uint32_t r = reg & 0x7F;
    // the two contexts' registers: their pair's low address, and the context
    auto contextRegister = [&](uint32_t base) -> uint64_t* {
        if (r != base && r != base + 1) return nullptr;
        GsDumpState::Context& c = s.context[r - base];
        switch (base) {
        case TEX0_1: return &c.tex0;
        case CLAMP_1: return &c.clamp;
        case TEX1_1: return &c.tex1;
        case XYOFFSET_1: return &c.xyoffset;
        case MIPTBP1_1: return &c.miptbp1;
        case MIPTBP2_1: return &c.miptbp2;
        case SCISSOR_1: return &c.scissor;
        case ALPHA_1: return &c.alpha;
        case FBA_1: return &c.fba;
        case FRAME_1: return &c.frame;
        case ZBUF_1: return &c.zbuf;
        default: return nullptr;
        }
    };
    for (uint32_t base : {TEX0_1, CLAMP_1, TEX1_1, XYOFFSET_1, MIPTBP1_1, MIPTBP2_1, SCISSOR_1, ALPHA_1, FBA_1, FRAME_1, ZBUF_1})
        if (uint64_t* p = contextRegister(base)) {
            *p = value;
            return;
        }
    switch (r) {
    case TEST_1: s.context[0].test = value; return;
    case TEST_1 + 1: s.context[1].test = value; return;
    case TEX2_1: case TEX2_1 + 1: {   // TEX2: TEX0's format and palette fields
        constexpr uint64_t mask = uint64_t(0x3F) << 20 | ~uint64_t(0) << 37;
        uint64_t& t = s.context[r - TEX2_1].tex0;
        t = (t & ~mask) | (value & mask);
        return;
    }
    case PRIM: s.prim = value & 0x7FF; return;
    case RGBAQ: s.rgbaq = value; return;
    case ST: s.st = value; return;
    case UV: s.uv = static_cast<uint32_t>(value & 0x3FFF3FFF); return;
    case FOG: s.fog = static_cast<uint32_t>(value >> 56) & 0xFF; return;
    case XYZF2: case XYZF3: s.xyz = value; s.fog = static_cast<uint32_t>(value >> 56) & 0xFF; return;
    case XYZ2: case XYZ3: s.xyz = value; return;
    case PRMODECONT: s.prmodecont = value & 1; return;
    case PRMODE: return;   // (the attributes when AC = 0; the state holds PRIM's)
    case TEXCLUT: s.texclut = value; return;
    case SCANMSK: s.scanmsk = value; return;
    case TEXA: s.texa = value; return;
    case FOGCOL: s.fogcol = value; return;
    case DIMX: s.dimx = value; return;
    case DTHE: s.dthe = value; return;
    case COLCLAMP: s.colclamp = value; return;
    case PABE: s.pabe = value; return;
    case BITBLTBUF: s.bitbltbuf = value; return;
    case TRXPOS: s.trxpos = value; return;
    case TRXREG: s.trxreg = value; return;
    default: return;   // TRXDIR (a transfer, completed by the frame's start), TEXFLUSH, SIGNAL, FINISH, LABEL...
    }
}

void GsDumpEncoder::addRegisters(const std::vector<std::pair<uint32_t, uint64_t>>& writes) {
    for (const auto& [reg, value] : writes) apply(reg, value);
    if (!writer.isOpen() || writes.empty()) return;
    for (size_t at = 0; at < writes.size(); at += 0x7FFF) {
        const size_t n = std::min<size_t>(0x7FFF, writes.size() - at);
        tag(static_cast<uint32_t>(n), true, 0, 1, AD);
        for (size_t k = 0; k < n; ++k) {
            word(writes[at + k].second);
            word(writes[at + k].first);
        }
    }
}

void GsDumpEncoder::image(const uint8_t* data, size_t bytes) {
    const size_t qwords = (bytes + 15) / 16;
    for (size_t at = 0; at < qwords; at += 0x7FFF) {
        const size_t n = std::min<size_t>(0x7FFF, qwords - at);
        tag(static_cast<uint32_t>(n), true, 2, 0, 0);
        const size_t from = at * 16, length = std::min(bytes - std::min(bytes, from), n * 16);
        const size_t start = out.size();
        out.resize(start + n * 16, 0);
        if (length) std::memcpy(out.data() + start, data + from, length);
    }
}

void GsDumpEncoder::transfer(uint32_t dbp, uint32_t dbw, uint32_t dpsm, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                             const uint8_t* data, size_t bytes) {
    addRegisters({{BITBLTBUF, uint64_t(dbp & 0x3FFF) << 32 | uint64_t(dbw & 0x3F) << 48 | uint64_t(dpsm & 0x3F) << 56},
                  {TRXPOS, uint64_t(x & 0x7FF) << 32 | uint64_t(y & 0x7FF) << 48},
                  {TRXREG, uint64_t(w & 0xFFF) | uint64_t(h & 0xFFF) << 32},
                  {TRXDIR, 0}});
    if (writer.isOpen()) image(data, bytes);
    put(3);
}

void GsDumpEncoder::gif(uint32_t path, const uint8_t* data, size_t bytes) {
    ++counts.records;
    parseGif(path, data, bytes);
    if (writer.isOpen() && bytes) {
        writer.transfer(path ? path : 3, data, bytes);
        ++counts.transfers;
        counts.bytes += bytes;
    }
}

// Raw GIF into the shadow: PACKED (A+D and the vertex registers' packed forms), REGLIST, IMAGE skipped.
void GsDumpEncoder::parseGif(uint32_t path, const uint8_t* data, size_t bytes) {
    Path& p = paths[path & 3];
    for (size_t at = 0; at + 16 <= bytes; at += 16) {
        uint64_t lo, hi;
        std::memcpy(&lo, data + at, 8);
        std::memcpy(&hi, data + at + 8, 8);
        if (p.imageLeft) {
            --p.imageLeft;
            if (!p.imageLeft) p.active = false;
            continue;
        }
        if (!p.active) {   // a tag
            p.nloop = static_cast<uint32_t>(lo & 0x7FFF);
            p.flg = static_cast<uint32_t>((lo >> 58) & 3);
            p.nreg = static_cast<uint32_t>((lo >> 60) & 0xF);
            if (!p.nreg) p.nreg = 16;
            p.regs = hi;
            p.reg = 0;
            if ((lo >> 46) & 1) apply(PRIM, (lo >> 47) & 0x7FF);
            if (!p.nloop) continue;
            if (p.flg >= 2) {
                p.imageLeft = p.nloop;
                p.active = true;
                continue;
            }
            p.active = true;
            continue;
        }
        if (p.flg == 1) {   // REGLIST: two registers a quadword
            for (const uint64_t v : {lo, hi}) {
                if (!p.active) break;
                const uint32_t reg = static_cast<uint32_t>((p.regs >> (4 * p.reg)) & 0xF);
                if (reg != AD && reg != NOP) apply(reg, v);
                if (++p.reg == p.nreg) {
                    p.reg = 0;
                    if (--p.nloop == 0) p.active = false;
                }
            }
            continue;
        }
        const uint32_t reg = static_cast<uint32_t>((p.regs >> (4 * p.reg)) & 0xF);   // PACKED
        switch (reg) {
        case AD: apply(static_cast<uint32_t>(hi & 0x7F), lo); break;
        case RGBAQ:
            apply(RGBAQ, (lo & 0xFF) | ((lo >> 32) & 0xFF) << 8 | (hi & 0xFF) << 16 | ((hi >> 32) & 0xFF) << 24 |
                             uint64_t(floatBits(internalQ)) << 32);
            break;
        case ST:
            apply(ST, lo);
            internalQ = std::bit_cast<float>(static_cast<uint32_t>(hi));
            registers.q = internalQ;
            break;
        case UV: apply(UV, (lo & 0x3FFF) | ((lo >> 32) & 0x3FFF) << 16); break;
        case XYZF2: {
            const bool adc = (hi >> 47) & 1;
            apply(adc ? XYZF3 : XYZF2, (lo & 0xFFFF) | ((lo >> 32) & 0xFFFF) << 16 | ((hi >> 4) & 0xFFFFFF) << 32 | ((hi >> 36) & 0xFF) << 56);
            break;
        }
        case XYZ2: {
            const bool adc = (hi >> 47) & 1;
            apply(adc ? XYZ3 : XYZ2, (lo & 0xFFFF) | ((lo >> 32) & 0xFFFF) << 16 | (hi & 0xFFFFFFFF) << 32);
            break;
        }
        case FOG: apply(FOG, ((hi >> 36) & 0xFF) << 56); break;
        case NOP: break;
        default: apply(reg, lo); break;
        }
        if (++p.reg == p.nreg) {
            p.reg = 0;
            if (--p.nloop == 0) p.active = false;
        }
    }
}

void GsDumpEncoder::state(const gfx::StateRecord& r) {
    ++counts.records;
    std::vector<std::pair<uint32_t, uint64_t>> writes;
    for (uint32_t g = 0; g < gfx::kSharedGroups; ++g)   // (gfx::applyState's order: shared, then context 1, then 2)
        if ((r.sharedGroups >> g) & 1) writes.emplace_back(gfx::kSharedRegister[g], sharedValue(r.shared, g));
    for (uint32_t context = 0; context < 2; ++context)
        for (uint32_t g = 0; g < gfx::kContextGroups; ++g)
            if ((r.contextGroups >> (16 * context + g)) & 1)
                writes.emplace_back(gfx::kContextRegister[g] + context, contextValue(r.context, r.palette, g));
    addRegisters(writes);
    put(3);
}

void GsDumpEncoder::upload(const gfx::UploadRecord& u, const uint8_t* data, size_t bytes) {
    ++counts.records;
    auto it = std::find(setUploads.begin(), setUploads.end(), std::make_pair(u.source, uint32_t(u.base)));
    if (it != setUploads.end()) {   // a texture set's upload, dumped with its set
        setUploads.erase(it);
        return;
    }
    if (!bytes && u.qwords && writer.isOpen()) ++counts.uploadsWithoutBytes;
    transfer(u.base, u.width, u.format, u.x, u.y, u.w, u.h, data, bytes);
}

void GsDumpEncoder::texSet(const uint8_t* payload, size_t bytes,
                           const std::function<const std::vector<uint8_t>*(uint64_t)>& kept) {
    ++counts.records;
    gfx::TexSetRecord r;
    if (bytes < sizeof r) return;
    std::memcpy(&r, payload, sizeof r);
    const size_t head = sizeof r + r.uploads * sizeof(gfx::TexSetUpload);
    if (head > bytes) return;
    std::vector<gfx::TexSetUpload> uploads(r.uploads);
    std::memcpy(uploads.data(), payload + sizeof r, r.uploads * sizeof(gfx::TexSetUpload));
    setUploads.clear();
    for (const gfx::TexSetUpload& u : uploads) setUploads.emplace_back(u.source, u.destination & 0x3FFF);
    if (!r.id) return;   // (a content kept with no set holding it: nothing in GS memory)
    // its bytes: the record's, or the content the host kept (same hash, same uploads' layout)
    const uint8_t* data = payload + head;
    size_t available = bytes - head;
    if (!r.sent) {
        const std::vector<uint8_t>* content = kept ? kept((uint64_t(r.hashHigh) << 32) | r.hashLow) : nullptr;
        if (!content) {
            if (writer.isOpen()) ++counts.setsWithoutBytes;
            data = nullptr;
            available = 0;
        } else {
            data = content->data();
            available = content->size();
        }
    }
    size_t offset = 0;
    for (const gfx::TexSetUpload& u : uploads) {
        const size_t n = size_t(u.qwords) * 16;
        const size_t have = data && offset < available ? std::min(n, available - offset) : 0;
        transfer(u.destination & 0x3FFF, (u.destination >> 16) & 0x3F, (u.destination >> 24) & 0x3F, 0, 0, u.w, u.h,
                 have ? data + offset : nullptr, have ? n : 0);
        offset += n;
    }
}

void GsDumpEncoder::read(const gfx::ReadRecord& r) {
    ++counts.records;
    addRegisters({{BITBLTBUF, uint64_t(r.base & 0x3FFF) | uint64_t(r.width & 0x3F) << 16 | uint64_t(r.format & 0x3F) << 24},
                  {TRXPOS, uint64_t(r.x & 0x7FF) | uint64_t(r.y & 0x7FF) << 16},
                  {TRXREG, uint64_t(r.w & 0xFFF) | uint64_t(r.h & 0xFFF) << 32},
                  {TRXDIR, 1}});
    put(3);
    if (writer.isOpen()) writer.readFifo(r.qwords & ~gfx::kReadAsync);
}

void GsDumpEncoder::beforeDraw(uint32_t prim) {
    const GsDumpState::Context& c = registers.context[(prim >> 9) & 1];
    const bool zte = (c.test >> 16) & 1;
    const uint32_t ztst = static_cast<uint32_t>((c.test >> 17) & 3), zbp = static_cast<uint32_t>(c.zbuf & 0x1FF);
    if (!writer.isOpen() || !zte) return;
    if (ztst >= 2 && !zWritten.count(zbp)) ++counts.depthUnwritten;
    if (!((c.zbuf >> 32) & 1)) zWritten.insert(zbp);
}

void GsDumpEncoder::draw(const gfx::DrawRecord& d, const gfx::Vertex* vertices) {
    ++counts.records;
    if (d.flags & gfx::kDrawPrim) addRegisters({{PRIM, uint64_t(d.prim & 0x7FF)}});
    const bool color = d.flags & gfx::kDrawColor, st = d.flags & gfx::kDrawSt, uv = d.flags & gfx::kDrawUv;
    const bool fog = d.flags & gfx::kDrawFog, attributesOnly = d.flags & gfx::kDrawAttributesOnly;
    if (!d.count) {
        put(3);
        return;
    }
    if (!attributesOnly && writer.isOpen()) {
        beforeDraw(static_cast<uint32_t>(registers.prim));
        ++counts.draws;
        counts.vertices += d.count;
    }
    // each vertex's registers, in runs of the same XYZ register (REGLIST's registers are fixed per tag)
    auto xyzRegister = [&](const gfx::Vertex& v) { return fog ? (v.kick ? XYZF2 : XYZF3) : (v.kick ? XYZ2 : XYZ3); };
    auto xyzValue = [&](const gfx::Vertex& v) {
        const uint64_t xy = uint64_t(v.x) | uint64_t(v.y) << 16;
        return fog ? xy | uint64_t(v.z & 0xFFFFFF) << 32 | uint64_t(v.fog) << 56 : xy | uint64_t(v.z) << 32;
    };
    auto rgbaqValue = [&](const gfx::Vertex& v) {
        const uint32_t rgba = color ? v.rgba : static_cast<uint32_t>(registers.rgbaq);
        return uint64_t(rgba) | uint64_t(floatBits(v.q)) << 32;
    };
    if (!writer.isOpen()) {   // the shadow only: the last vertex's values
        const gfx::Vertex& v = vertices[d.count - 1];
        if (color || st) apply(RGBAQ, rgbaqValue(v));
        if (st) apply(ST, uint64_t(floatBits(v.s)) | uint64_t(floatBits(v.t)) << 32);
        if (uv) apply(UV, uint64_t(v.u) | uint64_t(v.v) << 16);
        if (!attributesOnly) apply(xyzRegister(v), xyzValue(v));
        return;
    }
    for (uint32_t first = 0; first < d.count;) {
        const uint32_t xyz = attributesOnly ? NOP : xyzRegister(vertices[first]);
        uint32_t end = first + 1;
        while (end < d.count && end - first < 0x7FFF && (attributesOnly || xyzRegister(vertices[end]) == xyz)) ++end;
        uint32_t regs[4], nreg = 0;
        if (color || st) regs[nreg++] = RGBAQ;
        if (st) regs[nreg++] = ST;
        if (uv) regs[nreg++] = UV;
        if (!attributesOnly) regs[nreg++] = xyz;
        if (!nreg) break;
        uint64_t regsField = 0;
        for (uint32_t k = 0; k < nreg; ++k) regsField |= uint64_t(regs[k]) << (4 * k);
        tag(end - first, true, 1, nreg, regsField);
        for (uint32_t i = first; i < end; ++i) {
            const gfx::Vertex& v = vertices[i];
            for (uint32_t k = 0; k < nreg; ++k) {
                uint64_t value = 0;
                switch (regs[k]) {
                case RGBAQ: value = rgbaqValue(v); break;
                case ST: value = uint64_t(floatBits(v.s)) | uint64_t(floatBits(v.t)) << 32; break;
                case UV: value = uint64_t(v.u) | uint64_t(v.v) << 16; break;
                default: value = xyzValue(v); break;
                }
                apply(regs[k], value);
                word(value);
            }
        }
        if ((size_t(end - first) * nreg) & 1) word(0);   // (REGLIST pads to a quadword)
        first = end;
    }
    put(3);
}

void GsDumpEncoder::primitives(const ps2::GsPrimitives& p) {
    ++counts.records;
    if (p.setPrim) addRegisters({{PRIM, uint64_t(p.prim & 0x7FF)}});
    if (!p.count) {
        put(3);
        return;
    }
    if (writer.isOpen()) {
        beforeDraw(static_cast<uint32_t>(registers.prim));
        ++counts.draws;
        counts.vertices += p.count;
    }
    auto xyzRegister = [&](const ps2::GsVertex& v) { return p.fog ? (v.adc ? XYZF3 : XYZF2) : (v.adc ? XYZ3 : XYZ2); };
    auto xyzValue = [&](const ps2::GsVertex& v) {
        const uint64_t xy = uint64_t(v.x & 0xFFFF) | uint64_t(v.y & 0xFFFF) << 16;
        return p.fog ? xy | uint64_t(v.z & 0xFFFFFF) << 32 | uint64_t(v.fog & 0xFF) << 56 : xy | uint64_t(v.z) << 32;
    };
    if (!writer.isOpen()) {
        const ps2::GsVertex& v = p.vertices[p.count - 1];
        apply(RGBAQ, uint64_t(v.rgba) | uint64_t(floatBits(v.q)) << 32);
        apply(ST, uint64_t(floatBits(v.s)) | uint64_t(floatBits(v.t)) << 32);
        apply(xyzRegister(v), xyzValue(v));
        return;
    }
    for (size_t first = 0; first < p.count;) {
        const uint32_t xyz = xyzRegister(p.vertices[first]);
        size_t end = first + 1;
        while (end < p.count && end - first < 0x7FFF && xyzRegister(p.vertices[end]) == xyz) ++end;
        tag(static_cast<uint32_t>(end - first), true, 1, 3, uint64_t(RGBAQ) | uint64_t(ST) << 4 | uint64_t(xyz) << 8);
        for (size_t i = first; i < end; ++i) {
            const ps2::GsVertex& v = p.vertices[i];
            const uint64_t values[3] = {uint64_t(v.rgba) | uint64_t(floatBits(v.q)) << 32,
                                        uint64_t(floatBits(v.s)) | uint64_t(floatBits(v.t)) << 32, xyzValue(v)};
            apply(RGBAQ, values[0]);
            apply(ST, values[1]);
            apply(xyz, values[2]);
            for (const uint64_t value : values) word(value);
        }
        if (((end - first) * 3) & 1) word(0);
        first = end;
    }
    put(3);
}

void GsDumpEncoder::effect(const gfx::EffectRecord& r) {
    ++counts.records;
    if (r.kind == gfx::kGrade && writer.isOpen()) ++counts.effects;   // (a pass of the host's: not the GS's input)
}

void GsDumpEncoder::open(const uint8_t* memory, const char* source) {
    opened = true;
    GsDumpState start = registers;
    if (memory) start.memory.assign(memory, memory + GsDumpState::kMemoryBytes);
    memorySource = source ? source : "";
    std::vector<uint8_t> privileged(GsDumpState::kPrivilegedBytes);
    privilegedBlock(shown, 0, privileged.data());
    std::string error;
    if (!writer.open(options.path, start, privileged.data(), error)) {
        failed = true;
        std::fprintf(stderr, "GS dump: %s: %s\n", options.path.c_str(), error.c_str());
        return;
    }
    zWritten.clear();
    std::printf("GS dump: %s from VBlank %llu, %llu VSyncs; GS memory from %s\n", options.path.c_str(),
                static_cast<unsigned long long>(options.from), static_cast<unsigned long long>(options.count), memorySource.c_str());
}

void GsDumpEncoder::vsync(uint32_t field) {
    if (!writer.isOpen()) return;
    std::vector<uint8_t> privileged(GsDumpState::kPrivilegedBytes);
    privilegedBlock(shown, uint64_t(field & 1) << 13, privileged.data());
    writer.vsync(field & 1, privileged.data());
    if (++vsyncs < options.count) return;
    std::string error;
    if (!writer.close(error)) {
        failed = true;
        std::fprintf(stderr, "GS dump: %s: %s\n", options.path.c_str(), error.c_str());
    }
    closed = true;
    std::printf("GS dump: %s written: %s\n", options.path.c_str(), summary().c_str());
}

std::string GsDumpEncoder::summary() const {
    char line[512];
    if (!opened) {   // (FROM never came: say which VBlanks did)
        if (lastSeen == 0) return "";
        std::snprintf(line, sizeof line, "GS dump %s: not written, VBlank %llu never came (the game's VBlanks %llu-%llu passed)",
                      options.path.c_str(), static_cast<unsigned long long>(options.from), static_cast<unsigned long long>(firstSeen),
                      static_cast<unsigned long long>(lastSeen));
        return line;
    }
    std::snprintf(line, sizeof line,
                  "GS dump %s%s: VBlanks %llu-%llu, GS memory from %s, %llu VSyncs, %llu transfers (%llu bytes), %llu draws "
                  "(%llu vertices); not dumped: %llu GPU 3D batches, %llu colour-grade passes, %llu uploads without bytes, "
                  "%llu texture sets without bytes; %llu draws testing a Z buffer not drawn since the start (not exact)",
                  options.path.c_str(), failed ? " (failed)" : closed ? "" : " (open)", static_cast<unsigned long long>(options.from),
                  static_cast<unsigned long long>(options.from + options.count - 1), memorySource.c_str(),
                  static_cast<unsigned long long>(vsyncs), static_cast<unsigned long long>(counts.transfers),
                  static_cast<unsigned long long>(counts.bytes), static_cast<unsigned long long>(counts.draws),
                  static_cast<unsigned long long>(counts.vertices), static_cast<unsigned long long>(counts.models),
                  static_cast<unsigned long long>(counts.effects), static_cast<unsigned long long>(counts.uploadsWithoutBytes),
                  static_cast<unsigned long long>(counts.setsWithoutBytes), static_cast<unsigned long long>(counts.depthUnwritten));
    return line;
}

}  // namespace gt4::gs
