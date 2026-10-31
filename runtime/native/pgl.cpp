// GT4's graphics library (decomp unit 00494578, "pgl") drawing through the native interface
// (include/gt4/gfx.h, docs/GRAPHICS_API_PLAN.md) instead of writing GS packets.
//
// The library builds each frame in a packet buffer (context 0x70002000 + 0x1F8 is the write cursor) that
// the frame's end sends as the VIF1 DMA chain. Until every producer is native, the native records go into
// that buffer too, so that they stay in order with the packets that are left: a DMA CNT tag whose upper
// half ends with the record code (gfx::kStreamCode | qwords), followed by the record.
#include "../guest.h"
#include "../link.h"
#include "../log.h"
#include "gt4/link/protocol.h"
#include "gt4/gfx.h"
#include "gt4/unpack.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace gt4::rt;
namespace gfx = gt4::gfx;

namespace {

constexpr uint32_t kContext = 0x70002000;   // spr_base(): the library's state
constexpr uint32_t kCursor = 0x1F8;         // the packet buffer's write cursor
constexpr uint32_t kDirty = 0x1D8;          // u64: groups changed since the last flush
constexpr uint32_t kFlushed = 0x1E0;        // u64: groups ever flushed
constexpr uint32_t kEnabled = 0x1E8;        // u64: groups to flush (the contexts in use)
constexpr uint32_t kSharedShadow = 0x120;   // u64 per shared group, as GS register values
constexpr uint32_t kContextShadow = 0x170;  // u64 per context group (the same values for both contexts)
constexpr uint32_t kOutputFlags = 0x2C0;    // u64

uint32_t &cursor(uint32_t context) { return guest_ref<uint32_t>(context + kCursor); }

// The packet buffer fills from the cursor up; what is left of PATH3's area grows down from +0x1FC.
void check_room(uint32_t context, uint32_t at, uint32_t bytes) {
    const uint32_t top = guest_ref<uint32_t>(context + 0x1FC);
    if ((at & 0x0FFFFFFF) + bytes <= (top & 0x0FFFFFFF)) return;
    static bool warned = false;
    if (!warned) {
        warned = true;
        log_error("pgl: the frame's packet buffer is full (cursor %08x + %u bytes, top %08x): records overwrite what is above",
                  at, bytes, top);
    }
}

// Appends one record (`payload`, then `extra`) at the cursor of the library at `context`, behind its own DMA tag.
// (Records written one after the other could share a block, but the library writes at the cursor and moves it
// later in places, so a grown block can come to cover what was written meanwhile; the frame buffers are large
// enough for a tag per record, 00105FD8.)
void append(uint32_t context, uint32_t type, const void *payload, uint32_t bytes, const void *extra = nullptr,
            uint32_t extraBytes = 0) {
    const uint32_t total = bytes + extraBytes;
    const uint32_t qwords = gfx::recordQwords(total);
    const uint32_t at = cursor(context);
    check_room(context, at, 16 + qwords * 16);
    uint32_t *tag = static_cast<uint32_t *>(guest_ptr(at));
    tag[0] = 0x10000000u | qwords;   // CNT
    tag[1] = 0;
    tag[2] = 0;
    tag[3] = gfx::kStreamCode | qwords;
    uint8_t *out = static_cast<uint8_t *>(guest_ptr(at + 16));
    const gfx::Header h = gfx::header(type, total);
    std::memcpy(out, &h, sizeof h);
    if (bytes) std::memcpy(out + sizeof h, payload, bytes);
    if (extraBytes) std::memcpy(out + sizeof h + bytes, extra, extraBytes);
    std::memset(out + sizeof h + total, 0, qwords * 16 - sizeof h - total);
    cursor(context) = at + 16 + qwords * 16;
}

// The size in qwords of a w x h transfer in `format` (004A4CF0).
uint32_t transfer_qwords(uint32_t format, uint32_t w, uint32_t h) {
    const uint32_t texels = w * h;
    switch (format) {
    case gfx::C32: return (texels * 4 + 15) >> 4;
    case gfx::C24: return (texels * 3 + 15) >> 4;
    case gfx::C16: case gfx::C16S: return (texels * 2 + 15) >> 4;
    case gfx::T8: case gfx::T8H: return (texels + 15) >> 4;
    case gfx::T4: case gfx::T4HL: case gfx::T4HH: return (texels + 31) >> 5;
    default: return texels;
    }
}

// An upload record for `destination` (the library's form: base | width << 16 | format << 24).
gfx::UploadRecord upload_record(uint32_t destination, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t source, uint32_t qwords) {
    gfx::UploadRecord u{};
    u.base = static_cast<uint16_t>(destination & 0x3FFF);
    u.width = static_cast<uint8_t>((destination >> 16) & 0x3F);
    u.format = static_cast<uint8_t>((destination >> 24) & 0x3F);
    u.x = static_cast<uint16_t>(x);
    u.y = static_cast<uint16_t>(y);
    u.w = static_cast<uint16_t>(w);
    u.h = static_cast<uint16_t>(h);
    u.source = source & 0x0FFFFFFF;
    u.qwords = qwords;
    return u;
}

void append_upload(uint32_t context, const gfx::UploadRecord &u) { append(context, gfx::kUpload, &u, sizeof u); }

// Native textures (docs/NATIVE_V2.md): each texture set uploaded into a slot (00499728 -> 00499508) gets an id,
// sent as a set record. The sets in graphics memory now are kept by slot (blocks): a texture flushed inside one, by
// any means (a record's bind 0049CC48, the pglu calls, a mirror's own TEX0), names that set;
// a set uploaded over a slot, or a frame buffer set at it, ends the older set there.
struct Resident {
    uint32_t id, lo, hi;
};
std::vector<Resident> g_resident;
uint32_t g_next_set = 0;

void end_residents(uint32_t lo, uint32_t hi) {
    std::erase_if(g_resident, [&](const Resident &r) { return r.lo < hi && lo < r.hi; });
}

const Resident *resident_at(uint32_t block) {
    for (const Resident &r : g_resident)
        if (block >= r.lo && block < r.hi) return &r;
    return nullptr;
}

// The set whose slot holds an image at `base`, or 0 (its palette: as the CLUT buffer has it when drawn).
uint32_t set_holding(uint32_t base) {
    const Resident *in = resident_at(base);
    return in ? in->id : 0;
}

gfx::StateRecord empty_state() {
    gfx::StateRecord r;
    std::memset(&r, 0, sizeof r);
    r.shared = gfx::Shared();
    r.context = gfx::Context();
    r.palette = gfx::Texture();
    return r;
}

// A state record of the groups 004AB350 would flush now (and its bookkeeping: the groups leave the dirty
// set and join the flushed set); false when nothing is to be flushed.
bool take_state(uint32_t context, gfx::StateRecord &r) {
    uint64_t &dirty = guest_ref<uint64_t>(context + kDirty);
    const uint64_t flush = dirty & guest_ref<uint64_t>(context + kEnabled);
    if (!flush) return false;
    dirty &= ~flush;
    guest_ref<uint64_t>(context + kFlushed) |= flush;
    r = empty_state();
    r.sharedGroups = static_cast<uint32_t>(flush >> 32) & 0x3FF;
    r.contextGroups = (static_cast<uint32_t>(flush) & 0x1FFF) | ((static_cast<uint32_t>(flush >> 16) & 0x1FFF) << 16);
    for (uint32_t g = 0; g < gfx::kSharedGroups; ++g)
        if ((r.sharedGroups >> g) & 1) gfx::setSharedGroup(r.shared, g, guest_ref<uint64_t>(context + kSharedShadow + 8 * g));
    const uint32_t either = (r.contextGroups | (r.contextGroups >> 16)) & 0x1FFF;
    for (uint32_t g = 0; g < gfx::kContextGroups; ++g)
        if ((either >> g) & 1) {
            const uint64_t value = guest_ref<uint64_t>(context + kContextShadow + 8 * g);
            if (g == gfx::kPalette) gfx::setPalette(r.palette, value);   // TEX2, apart from TEX0
            else gfx::setContextGroup(r.context, g, value);
            if (g == gfx::kTexture) {
                gfx::Texture &t = r.context.texture;
                t.set = set_holding(t.base);
            } else if (g == gfx::kPalette) {   // (TEX2: with the image TEX0 holds)
                const uint32_t base = static_cast<uint32_t>(guest_ref<uint64_t>(context + kContextShadow + 8 * gfx::kTexture) & 0x3FFF);
                r.palette.set = set_holding(base);
            } else if (g == gfx::kFrame) {
                const uint32_t block = static_cast<uint32_t>(value & 0x1FF) * 32;   // FBP: 2048-word pages
                if (resident_at(block)) end_residents(block, block + 32);
            }
        }
    return true;
}

// A draw record (primitives) at the cursor of the library at `context`.
void draw(uint32_t context, uint32_t prim, uint32_t flags, const gfx::Vertex *v, uint32_t n) {
    const gfx::DrawRecord d{static_cast<uint16_t>(prim), static_cast<uint16_t>(n), flags};
    append(context, gfx::kDraw, &d, sizeof d, v, n * static_cast<uint32_t>(sizeof(gfx::Vertex)));
}

}  // namespace

extern "C" void gt4_gfx_set(uint32_t context, const uint64_t *pairs, int n);

namespace {

// Plays a packet template in GT4's packet format (the screen grids', gt4_gfx_grid): tags with a count, an end
// bit, optionally the primitive, and either register/value pairs (PACKED; GT4's templates use the address+data
// form) or register lists (REGLIST, two 64-bit values per quadword); the drawing state they set becomes state
// records, the vertex attributes and positions draw records.
void play_template(uint32_t context, const uint32_t *t, uint32_t qwords) {
    uint32_t prim = 0;
    bool primPending = false, fog = false;
    bool colorSet = false, stSet = false, uvSet = false;   // attributes the template sets (the others stay as they were)
    gfx::Vertex cur{};
    cur.q = 1.0f;
    std::vector<gfx::Vertex> verts;
    std::vector<uint64_t> pairs;
    auto flushVerts = [&] {
        if (verts.empty() && !primPending) return;
        draw(context, prim,
             (primPending ? gfx::kDrawPrim : 0) | (fog ? gfx::kDrawFog : 0) | (colorSet ? gfx::kDrawColor : 0) | (stSet ? gfx::kDrawSt : 0) |
                 (uvSet ? gfx::kDrawUv : 0),
             verts.data(), static_cast<uint32_t>(verts.size()));
        verts.clear();
        primPending = false;
    };
    auto flushState = [&] {
        if (pairs.empty()) return;
        gt4_gfx_set(context, pairs.data(), static_cast<int>(pairs.size() / 2));
        pairs.clear();
    };
    auto bits = [](uint32_t u) {
        float f;
        std::memcpy(&f, &u, 4);
        return f;
    };
    auto reg = [&](uint32_t address, uint64_t v) {
        bool isShared = false;
        uint32_t ctx = 0, group = 0;
        if (gfx::groupOfRegister(address, isShared, ctx, group)) {
            flushVerts();
            pairs.push_back(v);
            pairs.push_back(address);
            return;
        }
        switch (address) {
        case 0x00: flushState(); flushVerts(); prim = static_cast<uint32_t>(v) & 0x7FF; primPending = true; return;
        case 0x01:
            if (!colorSet) flushVerts();
            colorSet = true;
            cur.rgba = static_cast<uint32_t>(v);
            cur.q = bits(static_cast<uint32_t>(v >> 32));
            return;
        case 0x02:
            if (!stSet) flushVerts();
            stSet = true;
            cur.s = bits(static_cast<uint32_t>(v));
            cur.t = bits(static_cast<uint32_t>(v >> 32));
            return;
        case 0x03:
            if (!uvSet) flushVerts();
            uvSet = true;
            cur.u = static_cast<uint16_t>(v & 0xFFFF);
            cur.v = static_cast<uint16_t>((v >> 16) & 0xFFFF);
            return;
        case 0x0A: cur.fog = static_cast<uint8_t>(v >> 56); return;
        case 0x04: case 0x0C: case 0x05: case 0x0D: {   // XYZF2, XYZF3, XYZ2, XYZ3
            flushState();
            const bool f = address == 0x04 || address == 0x0C;
            if (f != fog && (!verts.empty() || primPending)) flushVerts();
            fog = f;
            gfx::Vertex v2 = cur;
            v2.x = static_cast<uint16_t>(v);
            v2.y = static_cast<uint16_t>(v >> 16);
            v2.z = f ? static_cast<uint32_t>((v >> 32) & 0xFFFFFF) : static_cast<uint32_t>(v >> 32);
            if (f) v2.fog = static_cast<uint8_t>(v >> 56);
            v2.kick = address == 0x04 || address == 0x05;
            verts.push_back(v2);
            return;
        }
        default: return;   // TEXFLUSH, NOP and the like: nothing to keep
        }
    };
    static const uint8_t kListRegister[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0xFF, 0x0C, 0x0D, 0xFF, 0xFF};
    uint32_t q = 0;
    while (q < qwords) {
        const uint64_t tag = t[q * 4] | static_cast<uint64_t>(t[q * 4 + 1]) << 32;
        const uint64_t regs = t[q * 4 + 2] | static_cast<uint64_t>(t[q * 4 + 3]) << 32;
        ++q;
        const uint32_t nloop = tag & 0x7FFF, flg = (tag >> 58) & 3;
        uint32_t nreg = (tag >> 60) & 0xF;
        if (!nreg) nreg = 16;
        if ((tag >> 46) & 1) reg(0x00, (tag >> 47) & 0x7FF);
        if (flg == 0) {   // PACKED: GT4's templates use the address+data form
            for (uint32_t l = 0; l < nloop; ++l)
                for (uint32_t r = 0; r < nreg && q < qwords; ++r, ++q) {
                    const uint32_t d = (regs >> (4 * r)) & 0xF;
                    const uint64_t lo = t[q * 4] | static_cast<uint64_t>(t[q * 4 + 1]) << 32;
                    if (d == 0xE) {
                        reg(t[q * 4 + 2] & 0x7F, lo);
                    } else if (d != 0xF) {
                        static int warned = 0;
                        if (warned++ < 4) log_warn("pgl: a packet template with PACKED register %x (only address+data is played)", d);
                    }
                }
        } else if (flg == 1) {   // REGLIST
            uint32_t k = 0;
            const uint32_t total = nloop * nreg;
            for (; k < total && q < qwords; ++k) {
                const uint32_t d = (regs >> (4 * (k % nreg))) & 0xF;
                const uint64_t v = t[q * 4 + (k & 1) * 2] | static_cast<uint64_t>(t[q * 4 + (k & 1) * 2 + 1]) << 32;
                if (kListRegister[d] != 0xFF) reg(kListRegister[d], v);
                if (k & 1) ++q;
            }
            if (k & 1) ++q;
        } else {
            q += nloop;
        }
        if ((tag >> 15) & 1) break;
    }
    flushState();
    flushVerts();
}

// A vertex at (x, y) (1/16 pixel) with the library's packed Z and fog word (XYZF: Z in the low 24 bits).
gfx::Vertex vertex_zf(uint32_t x, uint32_t y, uint32_t zf, bool kick) {
    gfx::Vertex v{};
    v.x = static_cast<uint16_t>(x);
    v.y = static_cast<uint16_t>(y);
    v.z = zf & 0xFFFFFF;
    v.fog = static_cast<uint8_t>(zf >> 24);
    v.q = 1.0f;
    v.kick = kick;
    return v;
}

void flush_state() { reinterpret_cast<void (*)()>(guest_ptr(0x0049FE28))(); }

}  // namespace

extern "C" {

// Game code's draws (compat/game/gt4_gfx.h).
static_assert(sizeof(gfx::Vertex) == 32);
void gt4_gfx_draw(uint32_t context, uint32_t prim, uint32_t flags, const gfx::Vertex *v, int n) {
    if (n > 0 || (flags & gfx::kDrawPrim)) draw(context, prim, flags, v, n > 0 ? static_cast<uint32_t>(n) : 0);   // n 0: the attributes only
}

// An upload in stream order (compat/game/gt4_gfx.h): `destination` in the library's form (base | width << 16 |
// format << 24), the rectangle (x, y, w, h), the pixels at `source`. Returns its size in qwords (0: nothing).
int gt4_gfx_upload(uint32_t context, uint32_t destination, int x, int y, int w, int h, uint32_t source) {
    const uint32_t qwords = transfer_qwords(destination >> 24, static_cast<uint32_t>(w), static_cast<uint32_t>(h));
    if (!qwords) return 0;
    append_upload(context, upload_record(destination, x, y, w, h, source, qwords));
    return static_cast<int>(qwords);
}

// The same, taking effect now (the original sent it on DMA channel 2 at once): the pixels and the record go to
// the host ahead of the frame being built.
int gt4_gfx_upload_now(uint32_t destination, int x, int y, int w, int h, uint32_t source) {
    const uint32_t qwords = transfer_qwords(destination >> 24, static_cast<uint32_t>(w), static_cast<uint32_t>(h));
    if (!qwords) return 0;
    struct {
        gfx::Header header;
        gfx::UploadRecord upload;
        uint8_t pad[8];
    } block{};
    static_assert(sizeof block == 32);   // 4 + 20, padded
    block.header = gfx::header(gfx::kUpload, sizeof(gfx::UploadRecord));
    block.upload = upload_record(destination, x, y, w, h, source, qwords);
    const uint32_t head[2] = {block.upload.source, qwords * 16};
    link_send(GT4_REC_GFX_DATA, head, sizeof head, guest_ptr(block.upload.source), qwords * 16);
    link_send(GT4_REC_GFX, &block, sizeof block);
    return static_cast<int>(qwords);
}

// GT4's 3D batches from game code (compat/game/gt4_gfx.h).
void gt4_gfx_batch_frame(uint32_t context) { append(context, gfx::kBatchFrame, nullptr, 0); }

void gt4_gfx_batch_write(uint32_t context, uint32_t qword, const void *data, int count, uint32_t lanes) {
    if (count <= 0) return;
    const gfx::BatchWrite w{static_cast<uint16_t>(qword), static_cast<uint16_t>(count), lanes};
    append(context, gfx::kBatchWrite, &w, sizeof w, data, static_cast<uint32_t>(count) * 16);
}

void gt4_gfx_batch_model(uint32_t context, uint32_t source, int qwords, uint32_t mask) {
    const gfx::BatchModel m{source & 0x0FFFFFFF, static_cast<uint32_t>(qwords), mask};
    append(context, gfx::kBatchModel, &m, sizeof m);
}

// `num` elements of GT4's compact format `format` (gt4/unpack.h) from `data` into the batch from `qword` on, with
// the cycle (cl, wl), the lane mask and the row (null: zeros), as the game's VIF UNPACK codes did: batch writes,
// one per run of quadwords with the same lanes.
void gt4_gfx_batch_unpack(uint32_t context, uint32_t qword, uint32_t format, int num, int usn, uint32_t cl, uint32_t wl, uint32_t mask,
                          const uint32_t *row, const void *data) {
    if (num <= 0) return;
    uint32_t rowCopy[4] = {};
    if (row) std::memcpy(rowCopy, row, 16);
    const uint32_t col[4] = {};
    const uint32_t words = gfx::unpackWords(format, static_cast<uint32_t>(num), cl, wl);
    // The data, read as far as it is mapped (a game array at the very top of a native stack is read past its end
    // when the count is rounded up; on the PS2 other memory followed): zeros past it.
    static thread_local std::vector<uint8_t> bytes;
    bytes.assign(size_t(words) * 4, 0);
    if (data && words) {
        const uint8_t *src = static_cast<const uint8_t *>(data);
        size_t done = 0;
        while (done < bytes.size()) {
            MEMORY_BASIC_INFORMATION mbi;
            if (!VirtualQuery(src + done, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) break;
            const size_t avail = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize - reinterpret_cast<uintptr_t>(src + done);
            const size_t take = std::min(avail, bytes.size() - done);
            std::memcpy(bytes.data() + done, src + done, take);
            done += take;
        }
    }
    struct Run {
        uint32_t first = 0, lanes = 0, count = 0;
        uint32_t values[256 * 4];
    };
    static thread_local Run run;
    run.count = 0;
    auto flush = [&] {
        if (run.count) gt4_gfx_batch_write(context, qword + run.first, run.values, static_cast<int>(run.count), run.lanes);
        run.count = 0;
    };
    gfx::unpackExpand(format, static_cast<uint32_t>(num), usn != 0, cl, wl, mask, 0, rowCopy, col, bytes.data(), bytes.size(), [&](uint32_t offset, const uint32_t value[4], unsigned lanes) {
                          if (!lanes) return;
                          if (run.count && (lanes != run.lanes || offset != run.first + run.count || run.count == 256)) flush();
                          if (!run.count) {
                              run.first = offset;
                              run.lanes = lanes;
                          }
                          std::memcpy(run.values + 4 * run.count, value, 16);
                          ++run.count;
                      });
    flush();
}

// The screen grids (004A81A8, 004A9470): the game builds a block (the grid's parameters at qword 0, a packet
// template in GT4's packet format at the offset in qword 0 .y) for one of two programs that send the template
// piece by piece with fields of it stepped before each piece (the steps are float additions on fields that hold
// packed integers: the exponents stay fixed, so the mantissas count). Here the program runs natively and each
// piece is played as state and draw records.
void gt4_gfx_grid(uint32_t context, uint32_t program, const void *block, int qwords) {
    if (qwords <= 0) return;
    std::vector<uint32_t> b(size_t(qwords) * 4);
    std::memcpy(b.data(), block, b.size() * 4);
    auto word = [&](uint32_t q, int lane) -> uint32_t { return q < static_cast<uint32_t>(qwords) ? b[q * 4 + lane] : 0; };
    auto half = [&](uint32_t q, int lane) { return static_cast<uint16_t>(word(q, lane)); };
    auto vu = [](uint32_t bits) {   // the VU's reading of a float: no denormals, no infinities
        if (!(bits & 0x7F800000u)) bits &= 0x80000000u;
        if ((bits & 0x7F800000u) == 0x7F800000u) bits = (bits & 0x80000000u) | 0x7F7FFFFFu;
        float f;
        std::memcpy(&f, &bits, 4);
        return f;
    };
    auto fl = [&](uint32_t q, int lane) { return vu(word(q, lane)); };
    // the template: from its offset to its end tag (EOP)
    const uint32_t start = half(0, 1);
    uint32_t length = 0;
    while (length < 1024) {
        const uint32_t w0 = word(start + length, 0), w1 = word(start + length, 1);
        const uint32_t nloop = w0 & 0x7FFF, flg = (w1 >> 26) & 3;
        uint32_t nreg = (w1 >> 28) & 0xF;
        if (!nreg) nreg = 16;
        length += 1 + (flg == 0 ? nloop * nreg : flg == 1 ? (nloop * nreg + 1) / 2 : nloop);
        if ((w0 >> 15) & 1) break;
    }
    std::vector<uint32_t> tpl(size_t(length) * 4);
    for (uint32_t q = 0; q < length; ++q)
        for (int k = 0; k < 4; ++k) tpl[q * 4 + k] = word(start + q, k);
    auto set = [&](uint32_t q, int lane, float value) {
        if (q < length) std::memcpy(&tpl[q * 4 + lane], &value, 4);
    };
    auto play = [&] { play_template(context, tpl.data(), length); };
    if (program == 0x3D50) {   // rows of columns: a counter at qword (1 .x) .x; eight vertex pairs from (1 .y) get z ramps
        float field = fl(2, 0), rampStart = fl(4, 2), rampStep = fl(4, 3), secondStart = fl(5, 2), secondStep = fl(5, 3);
        const float colStep = fl(2, 1), rowStep = fl(2, 2);
        const uint32_t target = half(1, 0), firstVertex = half(1, 1);
        for (uint32_t row = 0, rows = half(3, 3); row <= rows; ++row) {
            float z2 = rampStart, z3 = secondStart;
            for (uint32_t column = 0, columns = half(3, 2); column <= columns; ++column) {
                set(target, 0, field);
                for (uint32_t k = 0, v = firstVertex; k < 8; ++k, v += 3) {
                    set(v + 1, 2, z2);
                    z2 = z2 + rampStep;
                    set(v + 2, 2, z3);
                    z3 = z3 + secondStep;
                }
                field = field + colStep;
                play();
            }
            field = field + rowStep;
            rampStart *= 16.0f;
            rampStep *= 16.0f;
            secondStart *= 16.0f;
            secondStep *= 16.0f;
        }
    } else if (program == 0x3EA0) {   // pieces: four fields (word x of the qwords at qword 1's halves) step by counters
        float field[4], step[4];
        for (int k = 0; k < 4; ++k) {
            field[k] = fl(2 + k, 0);
            step[k] = fl(2 + k, 1);
        }
        for (uint32_t piece = 0, pieces = half(2, 2); piece <= pieces; ++piece) {
            for (int k = 0; k < 4; ++k) {
                set(half(1, k), 0, field[k]);
                field[k] = field[k] + step[k];
            }
            play();
        }
    } else {
        static int warned = 0;
        if (warned++ < 4) log_warn("pgl: grid program %04x unknown (nothing drawn)", program);
    }
}

// The batch run (itop 0xFFFFFFFF: the lighting chain the last run had).
void gt4_gfx_batch_run(uint32_t context, uint32_t entry, uint32_t itop) {
    const gfx::BatchRun r{entry, itop};
    append(context, gfx::kBatchRun, &r, sizeof r);
}

// State that game code sets in the library's encoding (its shadow keeps GS register values): {value,
// register} pairs, applied in order as state records (compat/game/gt4_gfx.h).
void gt4_gfx_set(uint32_t context, const uint64_t *pairs, int n) {
    gfx::StateRecord r;
    bool open = false;
    uint32_t blockContext = 0;
    auto flush = [&] {
        if (open) append(context, gfx::kState, &r, sizeof r);
        open = false;
    };
    for (int k = 0; k < n; ++k) {
        const uint64_t value = pairs[2 * k];
        const uint32_t reg = static_cast<uint32_t>(pairs[2 * k + 1]) & 0xFF;
        bool isShared = false;
        uint32_t ctx = 0, group = 0;
        if (!gfx::groupOfRegister(reg, isShared, ctx, group)) {
            static int warned = 0;
            if (warned++ < 8) log_warn("pgl: gt4_gfx_set: register %02x is not drawing state (ignored)", reg);
            continue;
        }
        const uint32_t bit = isShared ? 0 : 1u << (16 * ctx + group);
        // a new record when the group repeats or the other context comes in (one record: one context's values)
        if (open && ((isShared && ((r.sharedGroups >> group) & 1)) || (!isShared && (r.contextGroups & bit)) ||
                     (!isShared && r.contextGroups && blockContext != ctx)))
            flush();
        if (!open) {
            r = empty_state();
            open = true;
        }
        if (isShared) {
            r.sharedGroups |= 1u << group;
            gfx::setSharedGroup(r.shared, group, value);
        } else {
            blockContext = ctx;
            r.contextGroups |= bit;
            if (group == gfx::kPalette) gfx::setPalette(r.palette, value);
            else gfx::setContextGroup(r.context, group, value);
        }
    }
    flush();
}

// 004A6538(context, x0, y0, x1, y1, u0, v0, u1, v1): one textured sprite from (x0, y0) to (x1, y1) (1/16
// pixel, origin included) with texels (u0, v0)..(u1, v1) (1/16), the sprite colour (+0xC24) and the 2D Z and
// fog (+0x248); the attributes are the library's (+0x4A) with texturing and texel coordinates.
void gt4_replace_func_004A6538(uint32_t context, int x0, int y0, int x1, int y1, int u0, int v0, int u1, int v1) {
    flush_state();
    const uint32_t prim = (guest_ref<uint16_t>(context + 0x4A) | 0x116) & 0xFFFF;
    const uint32_t zf = guest_ref<uint32_t>(context + 0x248), rgba = guest_ref<uint32_t>(context + 0xC24);
    gfx::Vertex v[2] = {vertex_zf(x0, y0, zf, false), vertex_zf(x1, y1, zf, true)};
    v[0].u = static_cast<uint16_t>(u0);
    v[0].v = static_cast<uint16_t>(v0);
    v[1].u = static_cast<uint16_t>(u1);
    v[1].v = static_cast<uint16_t>(v1);
    v[0].rgba = v[1].rgba = rgba;
    draw(context, prim, gfx::kDrawPrim | gfx::kDrawFog | gfx::kDrawColor | gfx::kDrawUv, v, 2);
}

// 004A6140(x0, y0, x1, y1): one untextured sprite in the fill colour (+0xC20) from (x0, y0) to (x1, y1) in
// pixels, through the 2D offset (+0x26C, +0x288; half height in field mode, +7).
void gt4_replace_func_004A6140(int x0, int y0, int x1, int y1) {
    const uint32_t s = kContext;
    const int ox = guest_ref<int32_t>(s + 0x288);
    const int shift = guest_ref<uint8_t>(s + 7) ? 3 : 4;
    const uint32_t ax = guest_ref<uint16_t>(s + 0x26C) + (x0 + ox) * 16;
    const uint32_t bx = guest_ref<uint16_t>(s + 0x26C) + (x1 + ox) * 16;
    // the XY words as the original packed them (Y << 16 | X: an X past 16 bits carries into Y)
    const uint32_t a = ((guest_ref<uint16_t>(s + 0x26E) + guest_ref<int32_t>(s + 0x28C) * 16 + (y0 << shift)) << 16) | ax;
    const uint32_t b = ((guest_ref<uint16_t>(s + 0x26E) + guest_ref<int32_t>(s + 0x28C) * 16 + (y1 << shift)) << 16) | bx;
    flush_state();
    const uint32_t prim = (guest_ref<uint16_t>(s + 0x4A) | 6) & 0xFFEF;
    const uint32_t zf = guest_ref<uint32_t>(s + 0x248);
    gfx::Vertex v[2] = {vertex_zf(a & 0xFFFF, a >> 16, zf, false), vertex_zf(b & 0xFFFF, b >> 16, zf, true)};
    v[0].rgba = v[1].rgba = guest_ref<uint32_t>(s + 0xC20);
    draw(s, prim, gfx::kDrawPrim | gfx::kDrawFog | gfx::kDrawColor, v, 2);
}

// 0049E5F8(context, destination): the drawing state for a 3D batch. The original flushed the changed state
// as an A+D packet into the batch at `destination` (qword 217), which the renderer kicks before its strips;
// here the state goes as a record before the batch, and the batch's packet is left empty (NLOOP 0, EOP) so
// that an earlier one in that buffer is not kicked again.
void gt4_replace_func_0049E5F8(uint32_t context, uint32_t destination) {
    gfx::StateRecord r;
    if (take_state(context, r)) append(context, gfx::kState, &r, sizeof r);
    const uint32_t empty[4] = {0x8000, 0, 0, 0};   // NLOOP 0, EOP
    gt4_gfx_batch_write(context, destination & 0x3FF, empty, 1, 0xF);
}

// 0049FE28(): the drawing state for 2D packets (the original: an A+D packet through DIRECT), when the
// library outputs anything (outputFlags' upper half); otherwise the changes are dropped.
void gt4_replace_func_0049FE28(void) {
    if (!(guest_ref<uint64_t>(kContext + kOutputFlags) & 0xFFFF0000ull)) {
        guest_ref<uint64_t>(kContext + kDirty) = 0;
        return;
    }
    gfx::StateRecord r;
    if (take_state(kContext, r)) append(kContext, gfx::kState, &r, sizeof r);
}

// 00499508(set): the uploads of a texture set (one per record of `set`: {source, the destination's
// BITBLTBUF high word, width, height}). The original chained them on PATH3 (an A+D transfer packet and a REF
// to the pixels per record) and kept PATH3 in step with VIF1 by interrupts; here each is an upload record in
// the VIF1 stream at the bind, before the draws that use the set.
void gt4_replace_func_00499508(uint32_t set) {
    const uint32_t count = guest_ref<uint16_t>(set + 0x16);
    const uint32_t records = guest_ref<uint32_t>(set + 0x1C);
    // The set as a native resource, under a new id: its uploads (the walker hashes and sends the bytes) and its slot.
    std::vector<uint8_t> body;
    gfx::TexSetRecord r{};
    r.id = ++g_next_set ? g_next_set : ++g_next_set;
    r.size = guest_ref<uint16_t>(set + 0x12);
    uint32_t lo = ~0u;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t rec = records + 12 * i;
        gfx::TexSetUpload u{};
        u.destination = guest_ref<uint32_t>(rec + 4);
        u.w = guest_ref<uint16_t>(rec + 8);
        u.h = guest_ref<uint16_t>(rec + 10);
        u.source = guest_ref<uint32_t>(rec) & 0x0FFFFFFF;
        u.qwords = transfer_qwords(u.destination >> 24, u.w, u.h);   // the original switched on the whole byte
        if (!u.qwords) continue;
        guest_ref<int32_t>(kContext + 0x1030) += static_cast<int32_t>(u.qwords);   // the frame's upload total
        lo = std::min(lo, u.destination & 0x3FFF);
        ++r.uploads;
        const auto *b = reinterpret_cast<const uint8_t *>(&u);
        body.insert(body.end(), b, b + sizeof u);
    }
    append(kContext, gfx::kTexSet, &r, sizeof r, body.data(), static_cast<uint32_t>(body.size()));
    // GT4_SET_UPLOADS=1 (checking): the uploads into graphics memory too, as before (the host's GT4_NATIVE_TEXTURES=0)
    static const bool uploadsToo = std::getenv("GT4_SET_UPLOADS") != nullptr;
    if (uploadsToo)
        for (size_t k = 0; k < body.size(); k += sizeof(gfx::TexSetUpload)) {
            gfx::TexSetUpload u;
            std::memcpy(&u, body.data() + k, sizeof u);
            append_upload(kContext, upload_record(u.destination, 0, 0, u.w, u.h, u.source, u.qwords));
        }
    if (lo != ~0u) {
        const uint32_t hi = lo + (static_cast<uint32_t>(r.size) << 6);
        end_residents(lo, hi);
        g_resident.insert(g_resident.begin(), Resident{r.id, lo, hi});
    }
}

// 0049CC48(record): binds a texture record of a set (40 bytes: TEX0, TEX1, MIPTBP1, MIPTBP2, CLAMP) into the drawing
// state's shadow: TEX0 (bits 61-63, the palette load, left out of the shadow's own copy; with a load, TEX2 too, a
// CSA forced to 0 into a 0x13 format...), TEX1 with the LOD step of +0x44 added to K, the mip addresses when the
// filter uses them, CLAMP; the texture override (D_00624CC0: masks of TEX0, TEX1, CLAMP fields kept from the
// shadow) applies first.
void gt4_replace_func_0049CC48(uint32_t input) {
    const uint32_t s = kContext;
    uint64_t a = guest_ref<uint64_t>(input), b = guest_ref<uint64_t>(input + 8), c = guest_ref<uint64_t>(input + 32);
    uint64_t flags = guest_ref<uint64_t>(s + 0x1D8);
    const bool overridden = guest_ref<uint8_t>(0x00624CC0 + 28) != 0;
    if (overridden) {
        const uint64_t ma = guest_ref<uint64_t>(0x00624CC0), mb = guest_ref<uint64_t>(0x00624CC0 + 8), mc = guest_ref<uint64_t>(0x00624CC0 + 16);
        a = (a & ~ma) | (guest_ref<uint64_t>(s + 0x1C0) & ma);
        b = (b & ~mb) | (guest_ref<uint64_t>(s + 0x1B8) & mb);
        c = (c & ~mc) | (guest_ref<uint64_t>(s + 0x1A8) & mc);
    }
    guest_ref<uint64_t>(s + 0x1A8) = c;
    flags |= 0x06800680;
    guest_ref<uint64_t>(s + 0x1B8) = b;
    const uint64_t tex0 = a & (~0ull >> 3);
    guest_ref<uint64_t>(s + 0x1C0) = tex0;
    if (a >> 61) {
        if ((a >> 56) & 0x1F) {
            a &= ~0x1F00000003F00000ull;
            a |= 0x01300000;
        }
        guest_ref<uint64_t>(s + 0x1B0) = a;
        flags |= 0x01000100;
    }
    const int32_t mode = static_cast<int32_t>(b & 0x1C);
    if (mode != 0) {
        int32_t x = static_cast<int32_t>((b >> 32) & 0xFFF);
        const int32_t shift = static_cast<int32_t>((b >> 19) & 3);
        x = static_cast<int16_t>(x * 16);
        x += guest_ref<int32_t>(s + 0x44) << shift;
        uint64_t &tex1 = guest_ref<uint64_t>(s + 0x1B8);
        tex1 = (tex1 & ~(0xFFFull << 32)) | (static_cast<uint64_t>(static_cast<uint32_t>(x >> 4) & 0xFFF) << 32);
        flags |= 0x08000800;
        guest_ref<uint64_t>(s + 0x1C8) = guest_ref<uint64_t>(input + 16);
        if (static_cast<uint32_t>(mode) >= 13u) {
            flags |= 0x10001000;
            guest_ref<uint64_t>(s + 0x1D0) = guest_ref<uint64_t>(input + 24);
        }
    }
    guest_ref<uint64_t>(s + 0x1D8) = flags;
}

// 00499728(set): binds a texture set: its slot in graphics memory (004AA788 finds it there; else 004AA650
// allocates one, 00498828 moves the set's addresses to it and 00499508 uploads it) and marks the texture
// state for a flush (004A4910). Returns the slot, or -1. The original then paired a VIF1 FLUSH|I with a
// PATH3 IRQ tag (004A2038(1)) so that the draws waited for the upload; the uploads are in the stream now.
int gt4_replace_func_00499728(uint32_t set) {
    if (!guest_ref<uint16_t>(set + 0x12)) return -1;
    using Find = int (*)(uint32_t);
    using Allocate = int (*)(uint32_t, uint32_t);
    using Rebase = int (*)(uint32_t, int);
    using Upload = void (*)(uint32_t);
    using Flush = void (*)();
    int slot = reinterpret_cast<Find>(guest_ptr(0x004AA788))(set);
    if (slot < 0) {
        slot = reinterpret_cast<Allocate>(guest_ptr(0x004AA650))(static_cast<uint32_t>(guest_ref<uint16_t>(set + 0x12)) << 6, set);
        if (slot >= 0) {
            reinterpret_cast<Rebase>(guest_ptr(0x00498828))(set, slot);
            reinterpret_cast<Upload>(guest_ptr(0x00499508))(set);
            reinterpret_cast<Flush>(guest_ptr(0x004A4910))();
        }
    }
    return slot;
}

// 004A5178(address, format, width, x, y, w, h, destination): reads the w x h rectangle at (x, y) of the buffer
// at byte `address` (format, `width` pixels wide) into game memory at `destination`, once what the frame drew
// before it is done. The original put the transfer (BITBLTBUF ... FINISH, TRXDIR = 1) in the VIF1 stream behind
// a DMA tag with its interrupt bit and a download list for the DMA handler (004AB930), with a VIF1/PATH3
// handshake; here it is a read record, which the runtime serves when it reaches it (runtime/gs.cpp).
void gt4_replace_func_004A5178(uint32_t address, int format, int width, int x, int y, int w, int h, uint32_t destination) {
    reinterpret_cast<void (*)(uint32_t)>(guest_ptr(0x004AAFF0))(kContext);   // the slot placeholders cleared
    width += 63;
    address >>= 6;
    const uint32_t desc = (address & 0xFFFF) | ((static_cast<uint32_t>(width >> 6) & 0xFF) << 16) | (static_cast<uint32_t>(format) << 24);
    uint32_t qwords = static_cast<uint32_t>(w * h);
    switch (desc >> 24) {   // 004ABB88
    case 0: case 48: qwords = (qwords * 4 + 15) >> 4; break;
    case 1: case 49: qwords = (qwords * 3 + 15) >> 4; break;
    case 2: case 10: qwords = (qwords * 2 + 15) >> 4; break;
    case 19: case 27: qwords = (qwords + 15) >> 4; break;
    case 20: case 36: case 44: qwords = (qwords + 31) >> 5; break;
    default: qwords = 0; break;
    }
    gfx::ReadRecord read{};
    read.base = static_cast<uint16_t>(desc & 0x3FFF);
    read.width = static_cast<uint8_t>((desc >> 16) & 0x3F);
    read.format = static_cast<uint8_t>((desc >> 24) & 0x3F);
    read.x = static_cast<uint16_t>(x);
    read.y = static_cast<uint16_t>(y);
    read.w = static_cast<uint16_t>(w);
    read.h = static_cast<uint16_t>(h);
    read.destination = destination;
    read.qwords = qwords;
    append(kContext, gfx::kRead, &read, sizeof read);
}

// The font texture's upload (00499B60 sets it up, 00499C38 sends it with the pixels' address).
struct FontUpload {
    uint32_t destination = 0;
    int x = 0, y = 0, w = 0, h = 0;
} g_font_upload;

// 00499B60(address, format, width, x, y, w, h): the original built the transfer's set-up packet (BITBLTBUF ...
// IMAGE tag, D_00624C64) and a TEXFLUSH packet (D_00624C68) at the top of the frame's buffer and kept its size
// in qwords (D_00624C6C); here the destination and rectangle are kept for 00499C38, and the size as before.
void gt4_replace_func_00499B60(int address, int format, int width, int x, int y, int w, int h) {
    const uint32_t destination = (static_cast<uint32_t>(address >> 6) & 0xFFFF) | ((static_cast<uint32_t>(width >> 6) & 0xFF) << 16) |
                                 (static_cast<uint32_t>(format) << 24);
    g_font_upload = {destination, x, y, w, h};
    guest_ref<uint32_t>(0x00624C6C) = transfer_qwords(destination >> 24, static_cast<uint32_t>(w), static_cast<uint32_t>(h));
}

// 00499C38(address): the font texture from `address` with 00499B60's set-up, in the frame's order; the count at
// +0x102C adds its size. The original sent the set-up, the pixels and the TEXFLUSH packet through REF tags.
void gt4_replace_func_00499C38(uint32_t address) {
    const uint32_t qwords = guest_ref<uint32_t>(0x00624C6C);
    if (!qwords) return;
    const FontUpload &f = g_font_upload;
    append_upload(kContext, upload_record(f.destination, f.x, f.y, f.w, f.h, address, qwords));
    guest_ref<int32_t>(kContext + 0x102C) += static_cast<int32_t>(qwords);
}

// 00499640(set): a texture set's uploads at once (the original sent each on DMA channel 2 and waited): each
// entry {pixels, destination, width, height} is an upload that takes effect before the frame being built.
void gt4_replace_func_00499640(uint32_t set) {
    reinterpret_cast<void (*)()>(guest_ptr(0x004A20C8))();
    const uint32_t count = guest_ref<uint16_t>(set + 0x16);
    const uint32_t entries = guest_ref<uint32_t>(set + 0x1C);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t e = entries + 12 * i;
        gt4_gfx_upload_now(guest_ref<uint32_t>(e + 4), 0, 0, guest_ref<uint16_t>(e + 8), guest_ref<uint16_t>(e + 10), guest_ref<uint32_t>(e));
    }
}

// 00105FD8(buffers): the frame packet buffers, two used in turn ({first, second, size, current}): the original's
// were 640 KiB each in the image (D_006DEB00). Records take more room than the packets they replace (a state
// record holds every group's fields; a 2D vertex is 32 bytes), so natively each is 4 MiB, outside the 32 MB of
// game memory but below 0x10000000: the library keeps their addresses masked to 28 bits in its DMA tags.
void gt4_replace_func_00105FD8(uint32_t self) {
    constexpr uint32_t kSize = 4u << 20;
    static uint32_t base = [] {
        for (uint32_t at = 0x04000000; at < 0x0F000000; at += 0x01000000)
            if (void *p = VirtualAlloc(reinterpret_cast<void *>(static_cast<uintptr_t>(at)), 2 * kSize, MEM_RESERVE | MEM_COMMIT,
                                       PAGE_READWRITE))
                return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
        log_warn("pgl: no room below 0x10000000 for the frame buffers; the image's 640 KiB ones are used");
        return 0u;
    }();
    const uint32_t size = base ? kSize : 0xA0000;
    const uint32_t first = base ? base : 0x006DEB00;
    guest_ref<uint32_t>(self) = first;
    guest_ref<uint32_t>(self + 4) = first + size;
    guest_ref<uint32_t>(self + 8) = size;
    guest_ref<uint32_t>(self + 12) = 0;
}

// 004A12B8(channel, offset, size, source): uploaded a VU microprogram (MPG codes by DMA). GT4's VU1 programs are
// native renderers on the host (platform/gs/renderers3d.cpp): nothing to upload.
void gt4_replace_func_004A12B8(int, int, int, uint32_t) {}

// 004A72C0(context, mask): set the lane mask the next masked batch data use (VIF STMASK) and kept it at +0x40.
// The batch records carry their mask (gt4_gfx_batch_unpack, gt4_gfx_batch_model): only the bookkeeping is left.
void gt4_replace_func_004A72C0(uint32_t context, uint32_t mask) { guest_ref<uint32_t>(context + 0x40) = mask; }

// 004A2038(mode): after an upload, paired a VIF1 FLUSH|I (004A1F60) with a PATH3 IRQ tag (004A1FA8) so that the
// draws after it waited for the upload on PATH3 (modes 0, 1, 2: which side waits for which). Uploads are records
// in the stream's order now (00499508, 004A4F48, 004A4DE0): nothing to wait for.
void gt4_replace_func_004A2038(uint32_t) {}

// 004AAD78(cache, i): a texture slot released; it joins the end of the free list (state 3). The original
// also left a placeholder VIF code in the stream (004A1F60(cache, 0)) that a later reuse of the slot within
// the frame turned into a FLUSH|I, to hold that set's PATH3 upload until the draws before it were done (004AAA20,
// 004AA4A0). Uploads are in stream order now: no placeholder, so no handshake.
void gt4_replace_func_004AAD78(uint32_t cache, int i) {
    const uint32_t entries = cache + 0xC30;   // 20 bytes each: {placeholder, set, state, pad, prev, next, ...}
    guest_ref<uint32_t>(entries + 20 * (i + 1)) = 0;
    if (guest_ref<int32_t>(entries + 20 * (i + 1) + 4) == guest_ref<int32_t>(cache + 0xEC4)) guest_ref<int32_t>(cache + 0xEC4) = 0;
    const int old = guest_ref<int8_t>(cache + 0xEBD);
    if (old >= 0) guest_ref<int8_t>(entries + 20 * old + 11) = static_cast<int8_t>(i);
    else guest_ref<int8_t>(cache + 0xEBC) = static_cast<int8_t>(i);
    guest_ref<int8_t>(entries + 20 * i + 10) = static_cast<int8_t>(old);
    guest_ref<int8_t>(entries + 20 * i + 11) = -1;
    guest_ref<int8_t>(cache + 0xEBD) = static_cast<int8_t>(i);
    guest_ref<uint8_t>(entries + 20 * i + 8) = 3;
}

}  // extern "C"
