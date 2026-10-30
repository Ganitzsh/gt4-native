// GT4's graphics library (decomp unit 00494578, "pgl") drawing through the native interface
// (include/gt4/gfx.h, docs/GRAPHICS_API_PLAN.md) instead of writing GS packets.
//
// The library builds each frame in a packet buffer (context 0x70002000 + 0x1F8 is the write cursor) that
// the frame's end sends as the VIF1 DMA chain. Until every producer is native, the native records go into
// that buffer too, so that they stay in order with the packets that are left: a DMA CNT tag whose upper
// half ends with the record code (gfx::kStreamCode | qwords), followed by the records.
#include "../guest.h"
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

// Writes a DMA CNT tag with `codeWord2` and the record code in its upper half, then the records; returns the
// address after them. `payload` holds the records (headers included), padded to whole qwords.
uint32_t write_records(uint32_t at, uint32_t codeWord2, const void *payload, uint32_t qwords) {
    uint32_t *tag = static_cast<uint32_t *>(guest_ptr(at));
    tag[0] = qwords | 0x10000000u;   // CNT
    tag[1] = 0;
    tag[2] = codeWord2;
    tag[3] = gfx::kStreamCode | qwords;
    std::memcpy(guest_ptr(at + 16), payload, qwords * 16);
    return at + 16 + qwords * 16;
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

}  // namespace

extern "C" {

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
    struct UploadBlock {
        gfx::Header header;
        gfx::UploadRecord upload;
        uint8_t pad[4];
    };
    static_assert(sizeof(UploadBlock) == 32);
    uint32_t at = cursor(kContext);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t rec = records + 12 * i;
        const uint32_t destination = guest_ref<uint32_t>(rec + 4);
        const uint32_t w = guest_ref<uint16_t>(rec + 8), h = guest_ref<uint16_t>(rec + 10);
        const uint32_t format = destination >> 24;
        // the transfer's size in qwords (004A4CF0)
        uint32_t qwords = w * h;
        switch (format) {
        case gfx::C32: qwords = (qwords * 4 + 15) >> 4; break;
        case gfx::C24: qwords = (qwords * 3 + 15) >> 4; break;
        case gfx::C16: case gfx::C16S: qwords = (qwords * 2 + 15) >> 4; break;
        case gfx::T8: case gfx::T8H: qwords = (qwords + 15) >> 4; break;
        case gfx::T4: case gfx::T4HL: case gfx::T4HH: qwords = (qwords + 31) >> 5; break;
        default: break;
        }
        if (!qwords) continue;
        guest_ref<int32_t>(kContext + 0x1030) += static_cast<int32_t>(qwords);   // the frame's upload total
        UploadBlock block{};
        block.header = {gfx::kUpload, static_cast<uint32_t>(sizeof(gfx::UploadRecord))};
        block.upload.base = static_cast<uint16_t>(destination & 0x3FFF);
        block.upload.width = static_cast<uint8_t>((destination >> 16) & 0x3F);
        block.upload.format = static_cast<uint8_t>(format & 0x3F);
        block.upload.w = static_cast<uint16_t>(w);
        block.upload.h = static_cast<uint16_t>(h);
        block.upload.source = guest_ref<uint32_t>(rec) & 0x0FFFFFFF;
        block.upload.qwords = qwords;
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
