// GT4's graphics library (decomp unit 00494578, "pgl") drawing through the native interface
// (include/gt4/gfx.h, docs/GRAPHICS_API_PLAN.md) instead of writing GS packets.
//
// The library builds each frame in a packet buffer (context 0x70002000 + 0x1F8 is the write cursor) that
// the frame's end sends as the VIF1 DMA chain. Until every producer is native, the native records go into
// that buffer too, so that they stay in order with the packets that are left: a DMA CNT tag whose upper
// half ends with the record code (gfx::kStreamCode | qwords), followed by the records.
#include "../guest.h"
#include "../link.h"
#include "../log.h"
#include "gt4/link/protocol.h"
#include "gt4/gfx.h"

#include <cstdint>
#include <cstring>

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

// The packet buffer fills from the cursor up; what is left of PATH3's chain grows down from +0x1FC. Records
// take more room than some of the packets they replace (a 2D vertex: 32 bytes instead of 8 to 24).
void check_room(uint32_t context, uint32_t at, uint32_t bytes) {
    const uint32_t top = guest_ref<uint32_t>(context + 0x1FC);
    if ((at & 0x0FFFFFFF) + bytes <= (top & 0x0FFFFFFF)) return;
    static bool warned = false;
    if (!warned) {
        warned = true;
        log_error("pgl: the frame's packet buffer is full (cursor %08x + %u bytes, top %08x): records overwrite PATH3's chain",
                  at, bytes, top);
    }
}

// Writes a DMA CNT tag with `codeWord2` and the record code in its upper half, then the records; returns the
// address after them. `payload` holds the records (headers included), padded to whole qwords.
uint32_t write_records(uint32_t at, uint32_t codeWord2, const void *payload, uint32_t qwords) {
    check_room(kContext, at, 16 + qwords * 16);
    uint32_t *tag = static_cast<uint32_t *>(guest_ptr(at));
    tag[0] = qwords | 0x10000000u;   // CNT
    tag[1] = 0;
    tag[2] = codeWord2;
    tag[3] = gfx::kStreamCode | qwords;
    std::memcpy(guest_ptr(at + 16), payload, qwords * 16);
    return at + 16 + qwords * 16;
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

struct UploadBlock {
    gfx::Header header;
    gfx::UploadRecord upload;
    uint8_t pad[4];
};
static_assert(sizeof(UploadBlock) == 32);

// An upload record for `destination` (the library's form: base | width << 16 | format << 24).
UploadBlock upload_block(uint32_t destination, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t source, uint32_t qwords) {
    UploadBlock block{};
    block.header = {gfx::kUpload, static_cast<uint32_t>(sizeof(gfx::UploadRecord))};
    block.upload.base = static_cast<uint16_t>(destination & 0x3FFF);
    block.upload.width = static_cast<uint8_t>((destination >> 16) & 0x3F);
    block.upload.format = static_cast<uint8_t>((destination >> 24) & 0x3F);
    block.upload.x = static_cast<uint16_t>(x);
    block.upload.y = static_cast<uint16_t>(y);
    block.upload.w = static_cast<uint16_t>(w);
    block.upload.h = static_cast<uint16_t>(h);
    block.upload.source = source & 0x0FFFFFFF;
    block.upload.qwords = qwords;
    return block;
}

// A state record of the groups 004AB350 would flush now (and its bookkeeping: the groups leave the dirty
// set and join the flushed set); false when nothing is to be flushed.
struct StateBlock {
    gfx::Header header;
    gfx::StateRecord state;
    uint8_t pad[(16 - (sizeof(gfx::Header) + sizeof(gfx::StateRecord)) % 16) % 16];
};
static_assert(sizeof(StateBlock) % 16 == 0);

bool take_state(uint32_t context, StateBlock &block) {
    uint64_t &dirty = guest_ref<uint64_t>(context + kDirty);
    const uint64_t flush = dirty & guest_ref<uint64_t>(context + kEnabled);
    if (!flush) return false;
    dirty &= ~flush;
    guest_ref<uint64_t>(context + kFlushed) |= flush;
    std::memset(&block, 0, sizeof block);
    block.header = {gfx::kState, static_cast<uint32_t>(sizeof(gfx::StateRecord))};
    gfx::StateRecord &r = block.state;
    r.shared = gfx::Shared();
    r.context = gfx::Context();
    r.palette = gfx::Texture();
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
        }
    return true;
}

// Writes a draw record (primitives) at the cursor of the library at `context`.
void draw(uint32_t context, uint32_t prim, uint32_t flags, const gfx::Vertex *v, uint32_t n) {
    const uint32_t payload = static_cast<uint32_t>(sizeof(gfx::DrawRecord) + n * sizeof(gfx::Vertex));
    const uint32_t qwords = gfx::recordQwords(payload);
    uint32_t at = cursor(context);
    check_room(context, at, 16 + qwords * 16);
    uint32_t *tag = static_cast<uint32_t *>(guest_ptr(at));
    tag[0] = qwords | 0x10000000u;   // CNT
    tag[1] = 0;
    tag[2] = 0;
    tag[3] = gfx::kStreamCode | qwords;
    uint8_t *out = static_cast<uint8_t *>(guest_ptr(at + 16));
    const gfx::Header h{gfx::kDraw, payload};
    const gfx::DrawRecord d{prim, n, flags, 0};
    std::memcpy(out, &h, sizeof h);
    std::memcpy(out + sizeof h, &d, sizeof d);
    std::memcpy(out + sizeof h + sizeof d, v, n * sizeof(gfx::Vertex));
    const uint32_t used = static_cast<uint32_t>(sizeof h) + payload;
    std::memset(out + used, 0, qwords * 16 - used);
    cursor(context) = at + 16 + qwords * 16;
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
    const UploadBlock block = upload_block(destination, x, y, w, h, source, qwords);
    cursor(context) = write_records(cursor(context), 0, &block, sizeof block / 16);
    return static_cast<int>(qwords);
}

// The same, taking effect now (the original sent it on DMA channel 2 at once): the pixels and the record go to
// the host ahead of the frame being built.
int gt4_gfx_upload_now(uint32_t destination, int x, int y, int w, int h, uint32_t source) {
    const uint32_t qwords = transfer_qwords(destination >> 24, static_cast<uint32_t>(w), static_cast<uint32_t>(h));
    if (!qwords) return 0;
    const UploadBlock block = upload_block(destination, x, y, w, h, source, qwords);
    const uint32_t head[2] = {block.upload.source, qwords * 16};
    link_send(GT4_REC_GFX_DATA, head, sizeof head, guest_ptr(block.upload.source), qwords * 16);
    link_send(GT4_REC_GFX, &block, sizeof block);
    return static_cast<int>(qwords);
}

// State that game code sets in the library's encoding (its shadow keeps GS register values): {value,
// register} pairs, applied in order as state records (compat/game/gt4_gfx.h).
void gt4_gfx_set(uint32_t context, const uint64_t *pairs, int n) {
    StateBlock block;
    bool open = false;
    uint32_t blockContext = 0;
    auto flush = [&] {
        if (open) cursor(context) = write_records(cursor(context), 0, &block, sizeof block / 16);
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
        if (open && ((isShared && ((block.state.sharedGroups >> group) & 1)) || (!isShared && (block.state.contextGroups & bit)) ||
                     (!isShared && block.state.contextGroups && blockContext != ctx)))
            flush();
        if (!open) {
            std::memset(&block, 0, sizeof block);
            block.header = {gfx::kState, static_cast<uint32_t>(sizeof(gfx::StateRecord))};
            block.state.shared = gfx::Shared();
            block.state.context = gfx::Context();
            block.state.palette = gfx::Texture();
            open = true;
        }
        if (isShared) {
            block.state.sharedGroups |= 1u << group;
            gfx::setSharedGroup(block.state.shared, group, value);
        } else {
            blockContext = ctx;
            block.state.contextGroups |= bit;
            if (group == gfx::kPalette) gfx::setPalette(block.state.palette, value);
            else gfx::setContextGroup(block.state.context, group, value);
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
    uint32_t at = cursor(context);
    StateBlock block;
    if (take_state(context, block)) at = write_records(at, 0, &block, sizeof block / 16);
    uint32_t *tag = static_cast<uint32_t *>(guest_ptr(at));
    tag[0] = 1 | 0x10000000u;   // CNT, one qword
    tag[1] = 0;
    tag[2] = 0;
    tag[3] = (1u << 16) | 0x6C00C000u | destination;   // UNPACK V4-32, FLG, USN: one qword at the destination
    uint64_t *empty = static_cast<uint64_t *>(guest_ptr(at + 16));
    empty[0] = 0x8000;   // NLOOP 0, EOP
    empty[1] = 0;
    cursor(context) = at + 32;
}

// 0049FE28(): the drawing state for 2D packets (the original: an A+D packet through DIRECT), when the
// library outputs anything (outputFlags' upper half); otherwise the changes are dropped.
void gt4_replace_func_0049FE28(void) {
    if (!(guest_ref<uint64_t>(kContext + kOutputFlags) & 0xFFFF0000ull)) {
        guest_ref<uint64_t>(kContext + kDirty) = 0;
        return;
    }
    StateBlock block;
    if (take_state(kContext, block)) cursor(kContext) = write_records(cursor(kContext), 0, &block, sizeof block / 16);
}

// 00499508(set): the uploads of a texture set (one per record of `set`: {source, the destination's
// BITBLTBUF high word, width, height}). The original chained them on PATH3 (an A+D transfer packet and a REF
// to the pixels per record) and kept PATH3 in step with VIF1 by interrupts; here each is an upload record in
// the VIF1 stream at the bind, before the draws that use the set.
void gt4_replace_func_00499508(uint32_t set) {
    const uint32_t count = guest_ref<uint16_t>(set + 0x16);
    const uint32_t records = guest_ref<uint32_t>(set + 0x1C);
    uint32_t at = cursor(kContext);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t rec = records + 12 * i;
        const uint32_t destination = guest_ref<uint32_t>(rec + 4);
        const uint32_t w = guest_ref<uint16_t>(rec + 8), h = guest_ref<uint16_t>(rec + 10);
        const uint32_t qwords = transfer_qwords(destination >> 24, w, h);   // the original switched on the whole byte
        if (!qwords) continue;
        guest_ref<int32_t>(kContext + 0x1030) += static_cast<int32_t>(qwords);   // the frame's upload total
        const UploadBlock block = upload_block(destination, 0, 0, w, h, guest_ref<uint32_t>(rec), qwords);
        at = write_records(at, 0, &block, sizeof block / 16);
    }
    cursor(kContext) = at;
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
    struct {
        gfx::Header header;
        gfx::ReadRecord read;
        uint8_t pad[4];
    } block{};
    static_assert(sizeof block == 32);
    block.header = {gfx::kRead, static_cast<uint32_t>(sizeof(gfx::ReadRecord))};
    block.read.base = static_cast<uint16_t>(desc & 0x3FFF);
    block.read.width = static_cast<uint8_t>((desc >> 16) & 0x3F);
    block.read.format = static_cast<uint8_t>((desc >> 24) & 0x3F);
    block.read.x = static_cast<uint16_t>(x);
    block.read.y = static_cast<uint16_t>(y);
    block.read.w = static_cast<uint16_t>(w);
    block.read.h = static_cast<uint16_t>(h);
    block.read.destination = destination;
    block.read.qwords = qwords;
    cursor(kContext) = write_records(cursor(kContext), 0, &block, sizeof block / 16);
}

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
