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
#include "load_stats.h"
#include "log.h"
#include "runtime.h"
#include "vehicle.h"
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

extern "C" unsigned int gt4_scene_on(void);   // runtime/native/pgl.cpp
extern "C" int gt4_game_option(int id);         // runtime/sdk.cpp: the host's options (runtime.h kOption*)

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
    if (in_guest_ram(a, bytes)) {
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
    return in_guest_ram(a, bytes) ? p : nullptr;
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

// Where a walk's time goes (logged with the tick counts): inside link_send (copying, and waiting for room when the host
// is behind: it sleeps 1 ms at a time) and in synchronous reads (waiting for the host's pixels). One per kick, passed
// down the walk (kicks on two threads, or a nested one, each keep their own).
struct WalkTimes {
    int64_t send = 0, read = 0;   // QPC ticks
    // its longest synchronous read: how long, into which guest address, how many qwords (for the session's longest)
    int64_t longestRead = 0;
    uint32_t readDestination = 0, readQwords = 0;
};
int64_t qpc_now() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}
void timed_send(WalkTimes &times, uint32_t type, const void *payload, uint32_t size, const void *extra = nullptr,
                uint32_t extra_size = 0) {
    const int64_t start = qpc_now();
    link_send(type, payload, size, extra, extra_size);
    times.send += qpc_now() - start;
}

void send_data(WalkTimes &times, uint32_t source, uint32_t bytes) {
    const uint32_t head[2] = {source, bytes};
    const std::vector<uint8_t> data = read_mapped(ptr(source), bytes);
    timed_send(times, GT4_REC_GFX_DATA, head, sizeof head, data.data(), bytes);
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
void send_tex_set(WalkTimes &times, uint8_t *payload, uint32_t bytes) {
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
            timed_send(times, GT4_REC_GFX_DATA, head, sizeof head, data[k].data(), n);
        } else if (!g_verify_uploads) {
            const uint32_t head[2] = {uploads[k].source, n | gfx::kDataReference};
            timed_send(times, GT4_REC_GFX_DATA, head, sizeof head, &content, sizeof content);
        } else {
            std::vector<uint8_t> both(8 + n);
            std::memcpy(both.data(), &content, 8);
            std::memcpy(both.data() + 8, data[k].data(), std::min<size_t>(n, data[k].size()));
            const uint32_t head[2] = {uploads[k].source, n | gfx::kDataReference};
            timed_send(times, GT4_REC_GFX_DATA, head, sizeof head, both.data(), static_cast<uint32_t>(both.size()));
        }
    }
}

// The model packets' contents the host has (docs/NATIVE_V2.md, step 2): their bytes go once.
gfx::ContentFifo g_sent_models(gfx::kModelsKept);

// One record block: the bytes its records refer to, the block, then its reads.
void send_block(WalkTimes &times, uint8_t *block, uint32_t bytes) {
    if (g_forget_requested.exchange(false)) {
        g_sent_uploads = gfx::ContentFifo(gfx::kUploadsKept);
        g_sent_sets.clear();
        const uint32_t head[2] = {0, gfx::kDataReference | gfx::kDataKeep};
        timed_send(times, GT4_REC_GFX_DATA, head, sizeof head);
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
            send_data(times, u.source, u.qwords * 16);
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
                timed_send(times, GT4_REC_GFX_DATA, head, sizeof head, bytes, n);
            }
        } else if (h.type() == gfx::kTexSet && h.bytes() >= sizeof(gfx::TexSetRecord)) {
            send_tex_set(times, block + at + sizeof h, h.bytes());
        } else if (h.type() == gfx::kRead && h.bytes() >= sizeof(gfx::ReadRecord)) {
            gfx::ReadRecord r;
            std::memcpy(&r, payload, sizeof r);
            reads.push_back(r);
        }
        at += gfx::recordQwords(h.bytes()) * 16;
    }
    timed_send(times, GT4_REC_GFX, block, bytes);
    for (gfx::ReadRecord r : reads) {
        const bool async = (r.qwords & gfx::kReadAsync) != 0;
        r.qwords &= ~gfx::kReadAsync;
        // (asynchronous: the pixels land in place when the host sends them; the game does not wait)
        if (async) {
            if (uint8_t *at = ptr(r.destination); in_guest(at, size_t(r.qwords) * 16)) {
                link_read_image_async(r.qwords, at);
                continue;
            }
        }
        std::vector<uint8_t> pixels(size_t(r.qwords) * 16);
        const int64_t start = qpc_now();
        link_read_image(r.qwords, pixels.data());   // zeros without a host
        const int64_t took = qpc_now() - start;
        times.read += took;
        if (took > times.longestRead) {
            times.longestRead = took;
            times.readDestination = r.destination;
            times.readQwords = r.qwords;
        }
        for (uint32_t q = 0; q < r.qwords; ++q) std::memcpy(ptr(r.destination + q * 16), &pixels[q * 16], 16);
    }
}

// Walks a list from `start` (bit 31: in the scratchpad), sending its record blocks.
void walk(WalkTimes &times, uint32_t start) {
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
            if (first.type() == gfx::kScene && depth > 0) {
                // a scene record in a display list called from the frame's list would be sent once per call: dropped
                // (docs/modern/CONTRACTS.md a.5.1; one record per block, pgl.cpp's append). FrameEnd's counts then
                // differ, so the host sees it too.
                static uint32_t dropped = 0;
                if (dropped++ == 0) log_warn("frame: a scene record in a called display list (at %08x): dropped", at);
            } else if (first.type() < gfx::kRecordTypeEnd && sizeof first + first.bytes() <= bytes) {
                send_block(times, block, bytes);
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

// The frame clock's ticks during a walk (CONTRACTS.md a.5.1, scene records on only): queued, and sent by the kick
// after FrameEnd.
// - The clock's thread runs frame_tick inside on_frame, holding the kernel's g_lock with a guest thread possibly frozen
//   anywhere (inside the C runtime's heap too): nothing there may allocate, block on anything but g_tick_lock, or log.
//   So the queue is a fixed ring and the lock an SRWLOCK (no allocation, no lazy initialisation).
// - A guest thread holds g_tick_lock only inside a KernelScope (no interrupt freezes it there) and never across a
//   link_send (leaving the send's KernelScope takes g_lock, which the clock holds).
// - The kick decides once whether to defer (scene records on) and keeps that decision until its queue is drained, so
//   an option change mid-walk cannot reorder the VSyncs (lane B's review).
struct QueuedTick {
    ClockTick tick;
    LARGE_INTEGER at;
};
constexpr uint32_t kTickRing = 256;     // whole ticks: a walk 4.2 s long (about 18 KB)
constexpr uint32_t kTickOverflow = 1024;   // past that, each tick's field and count, in order (the display collapses to the newest)
SRWLOCK g_tick_lock = SRWLOCK_INIT;
uint32_t g_walking = 0;                 // kicks walking with deferral latched on (under g_tick_lock; > 1: nested or another thread's)
QueuedTick g_ring[kTickRing];           // ticks that came during them, in order (under g_tick_lock)
uint32_t g_ring_head = 0, g_ring_count = 0;
struct FieldCount {
    uint32_t field, count;
    LARGE_INTEGER at;
};
FieldCount g_overflow[kTickOverflow];   // the ticks after a full ring, in order (under g_tick_lock): no VSync is dropped
uint32_t g_overflow_head = 0, g_overflow_count = 0;
ClockTick g_overflow_last{};            // the newest of them whole (its display and capture window go with each)
std::atomic<bool> g_deferring{false};   // g_walking > 0 (read without the lock on the records-off path)
std::atomic<uint32_t> g_in_walk{0};     // walks under way, any (for the count with scene records off)
std::atomic<uint32_t> g_ticks_mid_walk{0}, g_ticks_deferred{0}, g_ticks_overflowed{0}, g_ticks_merged{0}, g_kicks_nested{0};
std::atomic<int64_t> g_longest_deferral{0};   // QPC ticks
int64_t g_longest_walk = 0;                   // QPC ticks (g_tick_lock)
std::atomic<uint32_t> g_walks{0}, g_walks_over16{0}, g_walks_over50{0};
int64_t g_longest_walk_send = 0, g_longest_walk_read = 0;   // the longest walk's time in link_send and in reads (g_tick_lock)
uint32_t g_longest_walk_vblank = 0;                          // and the VBlank it started at (g_tick_lock)
// The session's longest synchronous read (g_tick_lock): how long, into which guest address, how many qwords, at which
// VBlank's kick. (Which game code asked for it is left to whoever diagnoses a long one.)
int64_t g_longest_read = 0;
uint32_t g_longest_read_destination = 0, g_longest_read_qwords = 0, g_longest_read_vblank = 0;
std::atomic<void (*)(const ClockTick &)> g_tick_send{nullptr};   // the clock's sender (gs.cpp), for the queued ticks

struct TickLock {   // g_tick_lock, from a guest thread: inside a KernelScope
    KernelScope ks;
    TickLock() { AcquireSRWLockExclusive(&g_tick_lock); }
    ~TickLock() { ReleaseSRWLockExclusive(&g_tick_lock); }
};

// kOptionSceneRecords 2's cost at the kick (D14's acceptance): send_vehicles_frame() calls, the blocks sent (a race
// running), and their QPC time (the race check, the decodes, the send), in total and the longest. Kick thread only.
std::atomic<uint32_t> g_vehicle_calls{0}, g_vehicle_blocks{0};
std::atomic<int64_t> g_vehicle_ticks{0}, g_vehicle_longest{0};

void raise_to(std::atomic<int64_t> &most, int64_t v) {
    for (int64_t m = most.load(); v > m && !most.compare_exchange_weak(m, v);) {}
}

// From the kick's thread, never the clock's: every 3600 VBlanks, the ticks that came during a walk and their deferral.
void log_tick_counts() {
    static std::atomic<uint32_t> logged{0};
    uint32_t last = logged.load();
    const uint32_t bucket = kernel_frame_count() / 3600;
    if (bucket == last || !logged.compare_exchange_strong(last, bucket)) return;   // (one kick logs a bucket)
    int64_t longest, longestSend, longestRead, read;
    uint32_t longestAt, readDestination, readQwords, readAt;
    {
        TickLock lock;
        longest = g_longest_walk, longestSend = g_longest_walk_send, longestRead = g_longest_walk_read, longestAt = g_longest_walk_vblank;
        read = g_longest_read, readDestination = g_longest_read_destination, readQwords = g_longest_read_qwords;
        readAt = g_longest_read_vblank;
    }
    static LARGE_INTEGER rate{};
    if (!rate.QuadPart) QueryPerformanceFrequency(&rate);
    const double ms = 1000.0 / static_cast<double>(rate.QuadPart);
    log_info("frame: ticks during a walk %u (deferred %u, the longest by %.3f ms; past the ring %u, merged %u); walks %u, over "
             "16 ms %u, over 50 ms %u, the longest %.3f ms at VBlank %u (%.3f in link_send, %.3f in reads); the longest synchronous "
             "read %.3f ms at VBlank %u (%u qwords into %08x); nested kicks %u",
             g_ticks_mid_walk.load(), g_ticks_deferred.load(), g_longest_deferral.load() * ms, g_ticks_overflowed.load(),
             g_ticks_merged.load(), g_walks.load(), g_walks_over16.load(), g_walks_over50.load(), longest * ms, longestAt,
             longestSend * ms, longestRead * ms, read * ms, readAt, readQwords, readDestination, g_kicks_nested.load());
    if (const uint32_t calls = g_vehicle_calls.load())
        log_info("frame: vehicles only (kOptionSceneRecords 2): %u kicks, %u blocks sent; %.1f us a kick on average, the "
                 "longest %.1f us", calls, g_vehicle_blocks.load(), g_vehicle_ticks.load() * ms * 1000.0 / calls,
                 g_vehicle_longest.load() * ms * 1000.0);
}

}  // namespace

void gt4::rt::frame_forget_sent_contents() { g_forget_requested = true; }

// The clock's thread, under g_lock (see above: no allocation, no logging).
void gt4::rt::frame_tick(const ClockTick &tick, void (*send)(const ClockTick &)) {
    if (g_in_walk.load() != 0) ++g_ticks_mid_walk;
    if (!g_deferring.load() && !gt4_scene_on()) {   // records off and no kick deferring: as before, at the tick
        // (One window is left, at an on-to-off flip only: a kick that latched records on just before it may set
        // g_deferring after the check above, and this tick then lands inside its list. The host counts it as a VSync
        // inside a frame.)
        send(tick);
        return;
    }
    AcquireSRWLockExclusive(&g_tick_lock);
    g_tick_send = send;
    if (g_walking) {
        ++g_ticks_deferred;
        if (g_ring_count < kTickRing && g_overflow_count == 0) {
            QueuedTick &q = g_ring[(g_ring_head + g_ring_count++) % kTickRing];
            q.tick = tick;
            QueryPerformanceCounter(&q.at);
        } else if (g_overflow_count < kTickOverflow) {   // ring full: this tick's field and count, in order
            FieldCount &f = g_overflow[(g_overflow_head + g_overflow_count++) % kTickOverflow];
            f.field = tick.field;
            f.count = tick.count;
            QueryPerformanceCounter(&f.at);
            g_overflow_last = tick;
            ++g_ticks_overflowed;
        } else {   // both full (a walk over 21 s): the newest takes this one's field and count (one VSync fewer)
            FieldCount &f = g_overflow[(g_overflow_head + kTickOverflow - 1) % kTickOverflow];
            f.field = tick.field;
            f.count = tick.count;
            g_overflow_last = tick;
            ++g_ticks_merged;
        }
        ReleaseSRWLockExclusive(&g_tick_lock);
        return;
    }
    send(tick);   // (held: a kick cannot start its walk before this tick is on the link)
    ReleaseSRWLockExclusive(&g_tick_lock);
}

namespace {

// After FrameEnd: the queued ticks in order, with this walk still counted, so a new tick cannot overtake a queued one.
// The last kick still walking drains, whichever it is (another thread's may outlive this one: lane B's review); a kick
// with another still walking leaves the ticks to it. (With two kicking threads, a kick B may start while this one sends a
// popped tick outside the lock: that tick then goes out during B's first blocks, a VSync inside B's frame that the host
// counts, and the rest of the queue is left to B, in order.)
void drain_ticks() {
    for (;;) {
        QueuedTick q;
        {
            TickLock lock;
            if (g_walking > 1 || (g_ring_count == 0 && g_overflow_count == 0)) {
                if (--g_walking == 0) g_deferring = false;
                return;
            }
            if (g_ring_count) {
                q = g_ring[g_ring_head];
                g_ring_head = (g_ring_head + 1) % kTickRing;
                --g_ring_count;
            } else {   // the overflow, oldest first: the newest display with each tick's own field and count
                const FieldCount &f = g_overflow[g_overflow_head];
                q.tick = g_overflow_last;
                q.tick.field = f.field;
                q.tick.count = f.count;
                q.at = f.at;
                g_overflow_head = (g_overflow_head + 1) % kTickOverflow;
                --g_overflow_count;
            }
        }
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        raise_to(g_longest_deferral, now.QuadPart - q.at.QuadPart);
        if (const auto send = g_tick_send.load()) send(q.tick);
    }
}

// The scene records the kick sends itself, each a header and the record padded to whole qwords (gfx::recordQwords).
constexpr uint32_t kVehicleBytes = gfx::recordQwords(sizeof(gfx::scene::Vehicle)) * 16;   // 4 + 328: 21 qwords
constexpr uint32_t kBeginBytes = gfx::recordQwords(sizeof(gfx::scene::FrameBegin)) * 16;  // 4 + 16: two qwords
constexpr uint32_t kEndBytes = gfx::recordQwords(sizeof(gfx::scene::FrameEnd)) * 16;      // 4 + 20: two qwords
static_assert(kVehicleBytes == 336 && kBeginBytes == 32 && kEndBytes == 32);

void put_record(uint8_t *block, uint32_t &at, const void *record, uint32_t bytes, uint32_t padded) {
    const gfx::Header h = gfx::header(gfx::kScene, bytes);
    std::memcpy(block + at, &h, sizeof h);
    std::memcpy(block + at + sizeof h, record, bytes);
    at += padded;
}

// The running cars' Vehicle records (D6's reader, runtime/vehicle.h; one per running car, in entrant order, CONTRACTS.md
// a.5 rule 4) into `block` at `at`, for both modes' blocks. Returns how many.
int put_vehicles(uint8_t *block, uint32_t &at, const gfx::scene::Vehicle *cars, int count) {
    for (int i = 0; i < count; ++i) put_record(block, at, &cars[i], sizeof cars[i], kVehicleBytes);
    return count;
}

// FrameEnd as a record block of its own (CONTRACTS.md a.5.1), right after the walked list, with the frame's Vehicle
// records before it (a.5 rule 4, c990148). With scene records off for this kick (or in vehicles-only mode), a frame left
// open (they changed after its FrameBegin) is closed unsent, so a later FrameBegin opens anew.
void send_frame_end(bool send) {
    namespace scene = gfx::scene;
    scene::FrameEnd end{};
    uint32_t thread = 0;
    if (!scene_frame_end(&end, sizeof end, &thread) || !send) return;
    if (thread != GetCurrentThreadId()) {   // A3's check: the frame opened on another thread than its kick
        static bool warned = false;
        if (!warned) {
            warned = true;
            log_warn("frame: a scene frame opened on thread %lu, kicked on %lu", static_cast<unsigned long>(thread),
                     static_cast<unsigned long>(GetCurrentThreadId()));
        }
    }
    // one block, no allocation (the kick, on the game's thread): the Vehicles, then FrameEnd
    alignas(16) uint8_t block[car::kMaxCars * kVehicleBytes + kEndBytes] = {};
    scene::Vehicle cars[car::kMaxCars];
    uint32_t at = 0;
    put_vehicles(block, at, cars, vehicle_records(cars, static_cast<int>(car::kMaxCars)));
    put_record(block, at, &end, sizeof end, kEndBytes);
    link_send(GT4_REC_GFX, block, at);
}

// kOptionSceneRecords 2, vehicles only (D14's haptics; CONTRACTS.md a.1, 01532de): while a race runs, one block of its
// own at the kick: FrameBegin (kFrameVehiclesOnly), the Vehicles, FrameEnd (no view; begins 1). One block, so nothing
// comes between it and its FrameEnd without the clock's deferral (a.5.1). Nothing outside a race.
bool send_vehicles_frame() {
    namespace scene = gfx::scene;
    scene::Vehicle cars[car::kMaxCars];
    const int count = vehicle_records(cars, static_cast<int>(car::kMaxCars));
    if (count <= 0) return false;
    scene::FrameBegin begin;
    scene::FrameEnd end;
    scene_vehicles_frame(begin, end);
    alignas(16) uint8_t block[kBeginBytes + car::kMaxCars * kVehicleBytes + kEndBytes] = {};
    uint32_t at = 0;
    put_record(block, at, &begin, sizeof begin, kBeginBytes);
    put_vehicles(block, at, cars, count);
    put_record(block, at, &end, sizeof end, kEndBytes);
    link_send(GT4_REC_GFX, block, at);
    return true;
}
}  // namespace

// 004A11F8(pair): the frame's kick. `pair` (or the library's current one, +0x20C) holds where the frame's list
// starts and where its PATH3 list starts. The original started both DMA channels and let their interrupts tell
// the library when they were done; here the list is walked and sent at once.
extern "C" void gt4_replace_func_004A11F8(uint32_t pair) {
    load_stats_kick();   // (GT4_LOAD_STATS: the frames drawn during a load)
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
    // With scene records on, the clock's ticks wait until the list and its FrameEnd are sent (CONTRACTS.md a.5.1): decided
    // once here, kept until the drain. The mode is latched once a kick (a.1): 1 full, 2 vehicles only (no deferral).
    const int mode = gt4_game_option(static_cast<int>(kOptionSceneRecords));
    const bool scene = mode == 1;
    if (scene) {
        TickLock lock;
        if (g_walking++) ++g_kicks_nested;   // (another kick walking: nested, or another thread's)
        g_deferring = true;
    }
    WalkTimes times;
    const uint32_t vblank = kernel_frame_count();
    const int64_t start = qpc_now();
    ++g_in_walk;
    if (const uint32_t list = guest_ref<uint32_t>(pair)) walk(times, list);
    --g_in_walk;
    const int64_t took = qpc_now() - start;
    {
        static const int64_t ms16 = [] {
            LARGE_INTEGER rate;
            QueryPerformanceFrequency(&rate);
            return rate.QuadPart * 16 / 1000;
        }();
        ++g_walks;
        if (took > ms16) ++g_walks_over16;
        if (took > ms16 * 50 / 16) ++g_walks_over50;
        TickLock lock;
        if (took > g_longest_walk) {
            g_longest_walk = took;
            g_longest_walk_send = times.send;
            g_longest_walk_read = times.read;
            g_longest_walk_vblank = vblank;
        }
        if (times.longestRead > g_longest_read) {
            g_longest_read = times.longestRead;
            g_longest_read_destination = times.readDestination;
            g_longest_read_qwords = times.readQwords;
            g_longest_read_vblank = vblank;
        }
    }
    send_frame_end(scene);   // (in mode 2 a full frame left open is closed unsent first)
    if (mode == 2) {
        const int64_t v0 = qpc_now();
        if (send_vehicles_frame()) ++g_vehicle_blocks;
        const int64_t vt = qpc_now() - v0;
        ++g_vehicle_calls;
        g_vehicle_ticks += vt;
        raise_to(g_vehicle_longest, vt);
    }
    if (scene) drain_ticks();
    log_tick_counts();
    signal(1);
    signal(2);
}
