// The frame's command buffer, to the host (docs/GRAPHICS_API_PLAN.md).
//
// GT4's graphics library builds each frame in a buffer (runtime/native/pgl.cpp): blocks of native records
// (include/gt4/gfx.h), each behind a 16-byte header in the PS2's DMA tag layout (word 0: the block's length in
// quadwords and how the list goes on, word 1: an address, words 2 and 3: kStreamCode | length marks a record
// block), linked as the library always linked them: a block follows the one before (cnt), or the list goes on
// at an address (next), or calls a display list recorded earlier and comes back (call, ret), or ends (end).
// At the frame's end the library's kick (004A11F8) hands over the list: it is walked here and each record block
// goes to the host as it is (GT4_REC_GFX), with the bytes its uploads and model packets refer to ahead of it,
// and each read served as soon as its block is sent. Nothing of the PS2's DMA channels, their registers or
// their interrupts is left.
#include "guest.h"
#include "kernel.h"
#include "link.h"
#include "log.h"
#include "gt4/gfx.h"
#include "gt4/link/protocol.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <unordered_set>
#include <vector>

using namespace gt4::rt;
namespace gfx = gt4::gfx;

namespace {

constexpr uint32_t kContext = 0x70002000;   // the graphics library's state (spr_base())

// An address in a list: the scratchpad (bit 31 set, or its 0x70000000 range), game memory (physical, or its
// uncached mirrors), or else natively a thread's stack or the frame buffers (00105FD8), as is.
uint8_t *ptr(uint32_t addr) {
    if ((addr & 0x80000000u) || (addr & 0x70000000u) == 0x70000000u)
        return static_cast<uint8_t *>(guest_ptr(kScratchpad + (addr & 0x3FF0)));
    const uint32_t high = addr >> 28;
    if ((high == 0 || high == 2 || high == 3) && (addr & 0x0FFFFFFF) < kRamSize)
        return static_cast<uint8_t *>(guest_ptr(addr & 0x01FFFFFF));
    return static_cast<uint8_t *>(guest_ptr(addr));
}

// `bytes` at `p`, as far as they are mapped (zeros past: a game array at the very top of a stack is read past its
// end when a count is rounded up).
std::vector<uint8_t> read_mapped(const uint8_t *p, size_t bytes) {
    std::vector<uint8_t> out(bytes, 0);
    const uintptr_t a = reinterpret_cast<uintptr_t>(p);
    if (a >= kGuestSectionStart && a + bytes <= kRamSize) {
        std::memcpy(out.data(), p, bytes);
        return out;
    }
    size_t done = 0;
    while (done < bytes) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(p + done, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) break;
        const size_t avail = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize - (a + done);
        const size_t take = std::min(avail, bytes - done);
        std::memcpy(out.data() + done, p + done, take);
        done += take;
    }
    return out;
}

// `p` itself when all `bytes` lie in the guest's memory (read in place), else null (read_mapped's checks).
const uint8_t *in_guest(const uint8_t *p, size_t bytes) {
    const uintptr_t a = reinterpret_cast<uintptr_t>(p);
    return a >= kGuestSectionStart && a + bytes <= kRamSize ? p : nullptr;
}

// A content's identity for the host (game side only: the host keys on it and never works it out): four 32-bit lanes
// of xxHash32's round, independent of each other (one native multiply per word on this 32-bit build; the 64-bit
// multiply chain before took a 32-bit build three, in series), folded and mixed into 64 bits. The texture sets and
// model packets of every frame go through it (half the game process's time in a race before, under FEX more).
uint64_t identity_hash(const uint8_t *p, size_t n, uint64_t seed) {
    constexpr uint32_t P1 = 2654435761u, P2 = 2246822519u, P3 = 3266489917u, P4 = 668265263u, P5 = 374761393u;
    auto rotl = [](uint32_t x, int r) { return (x << r) | (x >> (32 - r)); };
    auto round = [&](uint32_t acc, uint32_t v) { return rotl(acc + v * P2, 13) * P1; };
    const uint32_t s0 = static_cast<uint32_t>(seed), s1 = static_cast<uint32_t>(seed >> 32);
    uint32_t a = s0 + P1 + P2, b = s0 + P2, c = s1, d = s1 - P1;
    size_t i = 0;
    for (; i + 16 <= n; i += 16) {
        uint32_t w[4];
        std::memcpy(w, p + i, 16);
        a = round(a, w[0]), b = round(b, w[1]), c = round(c, w[2]), d = round(d, w[3]);
    }
    uint32_t lo = rotl(a, 1) + rotl(b, 7) + rotl(c, 12) + rotl(d, 18) + static_cast<uint32_t>(n);
    uint32_t hi = (a ^ rotl(d, 5)) + rotl(b ^ c, 11) * P4 + static_cast<uint32_t>(n) * P5;
    for (; i + 4 <= n; i += 4) {
        uint32_t w;
        std::memcpy(&w, p + i, 4);
        lo = rotl(lo + w * P3, 17) * P4;
        hi = rotl(hi ^ (w * P1), 15) * P2;
    }
    for (; i < n; ++i) {
        lo = rotl(lo + p[i] * P5, 11) * P1;
        hi = rotl(hi ^ (p[i] * P3), 9) * P2;
    }
    auto avalanche = [](uint32_t x) {
        x ^= x >> 15, x *= P2, x ^= x >> 13, x *= P3, x ^= x >> 16;
        return x;
    };
    lo = avalanche(lo);
    hi = avalanche(hi + lo * P1);
    return (uint64_t(hi) << 32) | lo;
}

void send_data(uint32_t source, uint32_t bytes) {
    const uint32_t head[2] = {source, bytes};
    const std::vector<uint8_t> data = read_mapped(ptr(source), bytes);
    link_send(GT4_REC_GFX_DATA, head, sizeof head, data.data(), bytes);
}

// The texture sets' contents the host has (docs/NATIVE_V2.md): their bytes go once, while a set in graphics memory
// holds them (gfx::SetSlots: the host keeps the same).
std::unordered_set<uint64_t> g_sent_sets;
gfx::SetSlots g_set_slots;
gfx::ContentFifo g_sent_uploads(gfx::kUploadsKept);   // the set uploads' contents the host keeps
// GT4_VERIFY_UPLOADS=1 (testing): a reference carries the bytes too, and the host stops at the first that differ.
const bool g_verify_uploads = [] { const char *e = getenv("GT4_VERIFY_UPLOADS"); return e && *e && *e != '0'; }();
// The host lost a content it was counted on to keep (GT4_REC_HOST_CMD 5): everything goes again in full, after a
// mark at which both ends forget the uploads (gfx::kDataReference | gfx::kDataKeep).
std::atomic<bool> g_forget_requested{false};

// A texture set record (gfx::TexSetRecord, in the frame's buffer): its content hashed now, as the frame is sent (the
// game changes some sets in place: the PS2 uploaded what was there then), and its bytes sent when the host has not
// had that content.
void send_tex_set(uint8_t *payload, uint32_t bytes) {
    gfx::TexSetRecord r;
    std::memcpy(&r, payload, sizeof r);
    if (sizeof r + r.uploads * sizeof(gfx::TexSetUpload) > bytes) return;
    const auto *uploads = reinterpret_cast<const gfx::TexSetUpload *>(payload + sizeof r);
    std::vector<std::vector<uint8_t>> data(r.uploads);
    const uint32_t lo = gfx::SetSlots::of(r, uploads).lo;
    uint64_t hash = 0xCBF29CE484222325ull ^ r.uploads ^ uint64_t(r.size) << 16;
    for (uint32_t k = 0; k < r.uploads; ++k) {
        // (the bytes read in place where they can be; copied only to be sent)
        const uint32_t n = uploads[k].qwords * 16;
        const uint8_t *bytes = in_guest(ptr(uploads[k].source), n);
        if (!bytes) {
            data[k] = read_mapped(ptr(uploads[k].source), n);
            bytes = data[k].data();
        }
        // (the layout: each upload's place in the slot, its width, format and size)
        const uint64_t layout[2] = {((uploads[k].destination & 0x3FFF) - lo) | (uploads[k].destination & 0xFFFF0000u) |
                                        (uint64_t(uploads[k].w) << 32 | uint64_t(uploads[k].h) << 48),
                                    uploads[k].qwords};
        for (const uint64_t v : layout) hash = (hash ^ v) * 0x100000001B3ull;
        hash = (hash ^ identity_hash(bytes, n, hash)) * 0x9E3779B97F4A7C15ull;
        hash ^= hash >> 31;
    }
    r.hashLow = static_cast<uint32_t>(hash);
    r.hashHigh = static_cast<uint32_t>(hash >> 32);
    g_set_slots.add(gfx::SetSlots::of(r, uploads), [](uint64_t gone) { g_sent_sets.erase(gone); });
    r.sent = g_sent_sets.insert(hash).second ? 1u : 0u;
    std::memcpy(payload, &r, sizeof r);
    if (!r.sent) return;
    // each upload's bytes, or a reference to a content the host keeps (gfx::kDataReference)
    for (uint32_t k = 0; k < r.uploads; ++k) {
        const uint32_t n = uploads[k].qwords * 16;
        if (data[k].empty()) data[k] = read_mapped(ptr(uploads[k].source), n);
        const uint64_t content = gfx::contentHash(data[k].data(), data[k].size());
        if (g_sent_uploads.add(content, [](uint64_t) {})) {
            const uint32_t head[2] = {uploads[k].source, n};
            link_send(GT4_REC_GFX_DATA, head, sizeof head, data[k].data(), n);
        } else if (!g_verify_uploads) {
            const uint32_t head[2] = {uploads[k].source, n | gfx::kDataReference};
            link_send(GT4_REC_GFX_DATA, head, sizeof head, &content, sizeof content);
        } else {
            std::vector<uint8_t> both(8 + n);
            std::memcpy(both.data(), &content, 8);
            std::memcpy(both.data() + 8, data[k].data(), std::min<size_t>(n, data[k].size()));
            const uint32_t head[2] = {uploads[k].source, n | gfx::kDataReference};
            link_send(GT4_REC_GFX_DATA, head, sizeof head, both.data(), static_cast<uint32_t>(both.size()));
        }
    }
}

// The model packets' contents the host has (docs/NATIVE_V2.md, step 2): their bytes go once.
gfx::ContentFifo g_sent_models(gfx::kModelsKept);

// One record block: the bytes its records refer to, the block, then its reads.
void send_block(uint8_t *block, uint32_t bytes) {
    if (g_forget_requested.exchange(false)) {
        g_sent_uploads = gfx::ContentFifo(gfx::kUploadsKept);
        g_sent_sets.clear();
        const uint32_t head[2] = {0, gfx::kDataReference | gfx::kDataKeep};
        link_send(GT4_REC_GFX_DATA, head, sizeof head);
        log_warn("frame: the host asked for every texture content again");
    }
    std::vector<gfx::ReadRecord> reads;
    for (uint32_t at = 0; at + sizeof(gfx::Header) <= bytes;) {
        gfx::Header h;
        std::memcpy(&h, block + at, sizeof h);
        const uint8_t *payload = block + at + sizeof h;
        (void)payload;
        if (h.type() == gfx::kUpload && h.bytes() >= sizeof(gfx::UploadRecord)) {
            gfx::UploadRecord u;
            std::memcpy(&u, payload, sizeof u);
            send_data(u.source, u.qwords * 16);
        } else if (h.type() == gfx::kBatchModel && h.bytes() >= sizeof(gfx::BatchModel)) {
            gfx::BatchModel m;
            std::memcpy(&m, payload, sizeof m);
            // its bytes once per content (read in place where they can be)
            const uint32_t n = m.qwords * 16;
            std::vector<uint8_t> copy;
            const uint8_t *bytes = in_guest(ptr(m.source), n);
            if (!bytes) {
                copy = read_mapped(ptr(m.source), n);
                bytes = copy.data();
            }
            const uint64_t hash = identity_hash(bytes, n, 0xCBF29CE484222325ull ^ n);
            m.hashLow = static_cast<uint32_t>(hash);
            m.hashHigh = static_cast<uint32_t>(hash >> 32);
            m.sent = g_sent_models.add(hash, [](uint64_t) {}) ? 1u : 0u;
            std::memcpy(block + at + sizeof h, &m, sizeof m);
            if (m.sent) {
                const uint32_t head[2] = {m.source, n};
                link_send(GT4_REC_GFX_DATA, head, sizeof head, bytes, n);
            }
        } else if (h.type() == gfx::kTexSet && h.bytes() >= sizeof(gfx::TexSetRecord)) {
            send_tex_set(block + at + sizeof h, h.bytes());
        } else if (h.type() == gfx::kRead && h.bytes() >= sizeof(gfx::ReadRecord)) {
            gfx::ReadRecord r;
            std::memcpy(&r, payload, sizeof r);
            reads.push_back(r);
        }
        at += gfx::recordQwords(h.bytes()) * 16;
    }
    link_send(GT4_REC_GFX, block, bytes);
    for (const gfx::ReadRecord &r : reads) {
        std::vector<uint8_t> pixels(size_t(r.qwords) * 16);
        link_read_image(r.qwords, pixels.data());   // zeros without a host
        for (uint32_t q = 0; q < r.qwords; ++q) std::memcpy(ptr(r.destination + q * 16), &pixels[q * 16], 16);
    }
}

// Walks a list from `start` (bit 31: in the scratchpad), sending its record blocks.
void walk(uint32_t start) {
    uint32_t at = start;
    uint32_t stack[2];
    int depth = 0;
    for (int guard = 0; guard < (1 << 22); ++guard) {
        uint32_t h[4];
        std::memcpy(h, ptr(at), 16);
        const uint32_t qwc = h[0] & 0xFFFF, id = (h[0] >> 28) & 7, addr = h[1];
        const uint32_t self = at & 0x80000000u;   // a scratchpad list goes on in the scratchpad
        const uint32_t body = (at & ~0x80000000u) + 16;
        if ((h[3] >> 24 & 0x7F) == 0x08 && (h[2] >> 24 & 0x7F) != 0x20) {   // a record block (not STMASK's data word)
            const uint32_t bytes = (h[3] & 0xFFFF) * 16;
            uint8_t *block = ptr(id == 3 || id == 4 || id == 0 ? addr : body | self);
            gfx::Header first;
            std::memcpy(&first, block, sizeof first);
            if (first.type() <= gfx::kEffect && sizeof first + first.bytes() <= bytes) {
                send_block(block, bytes);
            } else {
                static int warned = 0;
                if (warned++ < 8)
                    log_warn("frame: a block marked as records that is not (at %08x: %08x %08x %08x %08x; data %08x %08x)", at, h[0], h[1],
                             h[2], h[3], reinterpret_cast<const uint32_t *>(block)[0], reinterpret_cast<const uint32_t *>(block)[1]);
            }
        } else if (qwc) {
            // each kind once (its tag type and the VIF command in its upper word) with its size and first words, and
            // how often blocks were left out so far
            static std::vector<uint64_t> seen;
            static uint32_t total = 0;
            ++total;
            const uint32_t *w = reinterpret_cast<const uint32_t *>(ptr(id == 3 || id == 4 || id == 0 ? addr : body | self));
            const uint64_t kind = static_cast<uint64_t>(id) << 32 | (h[3] >> 24 & 0x7F) << 8 | (h[2] >> 24 & 0x7F);
            if (std::find(seen.begin(), seen.end(), kind) == seen.end() && seen.size() < 32) {
                seen.push_back(kind);
                log_warn("frame: a list block of %u qwords that is not records (at %08x, id %u, codes %08x %08x; %u left out so far): "
                         "%08x %08x %08x %08x | %08x %08x %08x %08x | %08x %08x %08x %08x",
                         qwc, at, id, h[2], h[3], total, w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7], w[8], w[9], w[10], w[11]);
            }
        }
        switch (id) {
        case 1: at = (body | self) + qwc * 16; break;                 // cnt: the next block follows
        case 2: at = addr; break;                                    // next
        case 3: case 4: at = (body | self); break;                   // ref, refs: the next header follows
        case 5:                                                      // call: a display list, then back
            if (depth == 2) {
                log_warn("frame: display lists nested deeper than two (at %08x): the list ends", at);
                return;
            }
            stack[depth++] = (body | self) + qwc * 16;
            at = addr;
            break;
        case 6:                                                      // ret
            if (!depth) return;
            at = stack[--depth];
            break;
        default: return;                                             // end (7), refe (0)
        }
    }
    log_warn("frame: a list that does not end (from %08x)", start);
}

}  // namespace

void gt4::rt::frame_forget_sent_contents() { g_forget_requested = true; }

// 004A11F8(pair): the frame's kick. `pair` (or the library's current one, +0x20C) holds where the frame's list
// starts and where its PATH3 list starts. The original started both DMA channels and let their interrupts tell
// the library when they were done; here the list is walked and sent at once.
extern "C" void gt4_replace_func_004A11F8(uint32_t pair) {
    if (!pair) {
        pair = guest_ref<uint32_t>(kContext + 0x20C);
        if (!pair) return;
    }
    link_wait_for_host();   // (a host far behind: the game waits, as a PS2 game waits for its GS)
    using Sema = int (*)(int);
    const Sema wait = reinterpret_cast<Sema>(guest_ptr(0x004A0F08)), signal = reinterpret_cast<Sema>(guest_ptr(0x004A0F38));
    wait(1);   // the channels' semaphores
    wait(2);
    for (int k = 0; k < 4; ++k) guest_ref<uint8_t>(kContext + 0x65 + k) = 0;   // the library's transfer counters
    // (The PATH3 list, at pair + 4, is left: it holds nothing natively, and recording a display list relinks it
    // into the frame's list, whose blocks would be sent twice.)
    if (const uint32_t list = guest_ref<uint32_t>(pair)) walk(list);
    signal(1);
    signal(2);
}
