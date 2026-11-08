#include "link.h"

#include "gt4/link/protocol.h"
#include "guest.h"
#include "kernel.h"
#include "log.h"
#include "runtime.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

namespace gt4::rt {
namespace {

Gt4LinkHeader *g_hdr = nullptr;
uint8_t *g_g2h = nullptr, *g_h2g = nullptr;
FILE *g_capture = nullptr;
std::mutex g_send_mutex;
uint64_t g_seq = 0;
uint8_t g_pad[2][18];
bool g_pad_valid[2];
std::mutex g_pad_mutex;
std::recursive_mutex g_poll_mutex;  // the frame clock's poll and link_read_image
uint32_t g_image_id = 0;           // the download waiting for its GS_IMAGE (0: none)
uint8_t *g_image_dst = nullptr;
uint32_t g_image_qwc = 0;
bool g_image_done = false;
uint32_t g_image_next = 0;         // the last download's id (both kinds)
// The asynchronous downloads in flight (link_read_image_async), up to 8 tracked. The host copies the pixels when it
// draws the read's list and answers when the GPU has finished that frame: they land 2.7-3.0 VBlanks after the ask at
// 60 frames a second (never before 2; the PS2's within 1), later with a host behind. The game does not wait for them:
// waiting with 1, 2 or 3 in flight held it to 29, 58 and 60 game frames/s at George V on a desktop, and to 40 with
// a slow GPU (GT4_GPU_THROTTLE_MS=18). GT4_READ_ASYNC_DEPTH=N (1..8, testing): N tracked instead.
struct AsyncRead {
    uint32_t id = 0;
    uint8_t *dst = nullptr;
    uint32_t qwc = 0;
    uint32_t frame = 0;   // the VBlank it was asked for at
};
constexpr int kAsyncMax = 8;
constexpr uint32_t kAsyncMaxQwc = 512;   // 64 x 32 PSMCT32: the flare target is 64 x N, N = 8 in every race seen
AsyncRead g_async[kAsyncMax];
bool g_async_landed = false;       // one has arrived (the first is waited for)
int async_depth() {
    static const int depth = [] {
        const char *e = std::getenv("GT4_READ_ASYNC_DEPTH");
        const int n = e ? std::atoi(e) : kAsyncMax;
        return n < 1 ? 1 : n > kAsyncMax ? kAsyncMax : n;
    }();
    return depth;
}

int async_in_flight() {
    int n = 0;
    for (const AsyncRead &a : g_async) n += a.dst ? 1 : 0;
    return n;
}

// Their pixels, held here until the game's code that owns the destination runs again (link_async_deliver): landing
// 2-3 frames after the ask, straight into game memory they could have hit an object freed meanwhile (a race's end, a
// quit to the menu). The newest per destination; one older than kLandedFrames VBlanks is not delivered (an object
// at the same address since). Fixed storage: they land in link_poll, which runs in the frame's delivery, where nothing
// may allocate (a guest thread can be frozen inside the heap); a larger read is waited for (link_read_image_async).
struct Landed {
    uint8_t *dst = nullptr;
    uint8_t bytes[kAsyncMaxQwc * 16];
    uint32_t size = 0;
    uint32_t frame = 0;   // the VBlank it landed at
    bool fresh = false;
};
constexpr uint32_t kLandedFrames = 8;
Landed g_landed[kAsyncMax];
uint32_t g_async_landings = 0, g_async_delivered = 0, g_async_expired = 0, g_async_superseded = 0;   // (GT4_READ_STATS)

void async_land(uint8_t *dst, const uint8_t *bytes, size_t n) {
    Landed *slot = nullptr;
    for (Landed &l : g_landed)
        if (l.dst == dst) slot = &l;
    for (Landed &l : g_landed)
        if (!slot && (!l.fresh || g_hdr->frame - l.frame > kLandedFrames)) slot = &l;
    if (!slot) slot = &g_landed[0];
    if (slot->fresh) ++g_async_superseded;   // (a newer one before the game ran: the older never delivered)
    slot->dst = dst;
    slot->size = static_cast<uint32_t>(n < sizeof slot->bytes ? n : sizeof slot->bytes);
    std::memcpy(slot->bytes, bytes, slot->size);
    slot->frame = g_hdr->frame;
    slot->fresh = true;
    ++g_async_landings;
}

// GT4_READ_STATS=1 (testing): how many VBlanks after it was asked for each asynchronous read landed, per 300.
void async_landed_after(uint32_t vblanks) {
    static const bool on = [] { const char *e = std::getenv("GT4_READ_STATS"); return e && *e && *e != '0'; }();
    if (!on) return;
    static uint32_t n = 0, sum = 0, most = 0, hist[6] = {};
    ++n;
    sum += vblanks;
    if (vblanks > most) most = vblanks;
    ++hist[vblanks < 5 ? vblanks : 5];
    if (n < 300) return;
    log_info("link: asynchronous reads landed %.2f VBlanks after the ask on average (most %u; 0/1/2/3/4/5+: %u/%u/%u/%u/%u/%u); "
             "so far %u landed, %u delivered, %u expired, %u superseded",
             double(sum) / n, most, hist[0], hist[1], hist[2], hist[3], hist[4], hist[5], g_async_landings, g_async_delivered,
             g_async_expired, g_async_superseded);
    n = sum = most = 0;
    for (uint32_t &h : hist) h = 0;
}

void ring_write(uint8_t *data, uint32_t size, uint64_t pos, const void *src, uint32_t n) {
    const uint32_t off = static_cast<uint32_t>(pos % size);
    const uint32_t first = n < size - off ? n : size - off;
    std::memcpy(data + off, src, first);
    if (n > first) std::memcpy(data, static_cast<const uint8_t *>(src) + first, n - first);
}

void ring_read(const uint8_t *data, uint32_t size, uint64_t pos, void *dst, uint32_t n) {
    const uint32_t off = static_cast<uint32_t>(pos % size);
    const uint32_t first = n < size - off ? n : size - off;
    std::memcpy(dst, data + off, first);
    if (n > first) std::memcpy(static_cast<uint8_t *>(dst) + first, data, n - first);
}

// A ring's 64-bit head or tail, read or written in ONE 8-byte access. This game is i686: a plain uint64_t is two
// 32-bit accesses (movl low word, then movl high word), and the host on the other side reads and writes the same
// word at any time. A torn value is off by 4 GiB whenever the low word wraps (every 4 GiB of traffic, several times a
// long session): a head read 4 GiB ahead makes the host take garbage for records, a tail read 4 GiB ahead makes the
// game overwrite records the host has not read. __atomic_load_n / __atomic_store_n on 8 aligned bytes are one SSE2
// movq here (the runtime is built with -msse2; the rings' words sit at 8-byte offsets in the page-aligned section),
// single-copy atomic on x86 and under FEX (docs/modern/spikes/R4-link-across-fex.md).
uint64_t load_index(const volatile uint64_t *index) { return __atomic_load_n(index, __ATOMIC_ACQUIRE); }
void store_index(volatile uint64_t *index, uint64_t value) { __atomic_store_n(index, value, __ATOMIC_RELEASE); }

}  // namespace

bool link_open(const char *name) {
    // --link NAME: the host's named section (Local\gt4_link_<pid>). --link PATH (a path: it has a slash, a backslash
    // or a drive colon, and is no Local\ or Global\ name): a file the host made, mapped by path. That is how the link
    // crosses to a native Linux host from under Wine (Z:\dev\shm\gt4_link_<pid>; docs/modern/spikes/
    // R4-link-across-fex.md); on Windows the host makes one with GT4_LINK_FILE.
    const bool named = !std::strncmp(name, "Local\\", 6) || !std::strncmp(name, "Global\\", 7);
    const bool file = !named && (std::strchr(name, '\\') || std::strchr(name, '/') || std::strchr(name, ':'));
    HANDLE h = nullptr;
    if (file) {
        HANDLE f = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) {
            log_error("link: cannot open the file %s (Windows error %lu)", name, GetLastError());
            return false;
        }
        h = CreateFileMappingA(f, nullptr, PAGE_READWRITE, 0, GT4_LINK_SIZE, nullptr);
        const DWORD e = GetLastError();   // (before CloseHandle, which may change it)
        CloseHandle(f);                   // (the mapping keeps the file open)
        if (!h) {
            log_error("link: cannot map the file %s (Windows error %lu)", name, e);
            return false;
        }
    } else {
        h = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name);
    }
    if (!h) {
        log_error("link: cannot open %s (Windows error %lu)", name, GetLastError());
        return false;
    }
    // The view must stay above the guest range (MapViewOfFile picks a free address; the guest
    // RAM is the image's own section, so it cannot land there).
    void *view = MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, GT4_LINK_SIZE);
    if (!view) {
        log_error("link: cannot map %s", name);
        return false;
    }
    g_hdr = static_cast<Gt4LinkHeader *>(view);
    if (g_hdr->magic != GT4_LINK_MAGIC || g_hdr->version != GT4_LINK_VERSION) {
        log_error("link: %s is not a version %u link", name, GT4_LINK_VERSION);
        g_hdr = nullptr;
        return false;
    }
    // The rings within the mapped size (a host and a game built from different protocol sizes would otherwise
    // read and write past the section; a file the host made is grown silently by a larger mapping).
    const uint64_t g2h_end = uint64_t(g_hdr->g2h_offset) + g_hdr->g2h_size, h2g_end = uint64_t(g_hdr->h2g_offset) + g_hdr->h2g_size;
    if (g2h_end > GT4_LINK_SIZE || h2g_end > GT4_LINK_SIZE || g_hdr->g2h_offset < sizeof(Gt4LinkHeader) ||
        g_hdr->h2g_offset < sizeof(Gt4LinkHeader)) {
        log_error("link: %s has rings outside its %u bytes (g2h %u+%u, h2g %u+%u)", name, GT4_LINK_SIZE,
                  g_hdr->g2h_offset, g_hdr->g2h_size, g_hdr->h2g_offset, g_hdr->h2g_size);
        g_hdr = nullptr;
        return false;
    }
    g_g2h = static_cast<uint8_t *>(view) + g_hdr->g2h_offset;
    g_h2g = static_cast<uint8_t *>(view) + g_hdr->h2g_offset;
    g_hdr->game_pid = GetCurrentProcessId();
    g_hdr->game_state = 1;
    log_info("link: connected to %s", name);
    return true;
}

// GT4_CAPTURE_FROM=VBLANK, GT4_CAPTURE_FRAMES=N: write only the records of those VBlanks (the GS
// back end replays from there: gs.cpp resends the privileged registers when the window opens).
uint32_t g_capture_from = 0, g_capture_frames = 0;

// Records a capture window must start with: the VU1 microprograms, uploaded once at boot.
std::vector<std::vector<uint8_t>> g_sticky;
bool g_window_started = false;

void link_remember(uint32_t type, const void *payload, uint32_t size) {
    KernelScope ks;   // see link_send
    std::lock_guard<std::mutex> lock(g_send_mutex);
    std::vector<uint8_t> rec(sizeof(uint32_t) + size);
    std::memcpy(rec.data(), &type, 4);
    std::memcpy(rec.data() + 4, payload, size);
    g_sticky.push_back(std::move(rec));
}

bool link_capture_window() {
    if (!g_capture) return false;
    const uint32_t v = kernel_frame_count();
    return v >= g_capture_from && (!g_capture_frames || v - g_capture_from < g_capture_frames);
}

bool link_capture_finished() {
    return g_capture && g_capture_frames && kernel_frame_count() >= g_capture_from + g_capture_frames;
}

void link_capture_close() {
    KernelScope ks;
    std::lock_guard<std::mutex> lock(g_send_mutex);
    if (g_capture) fclose(g_capture);
    g_capture = nullptr;
}

bool link_capture(const char *path) {
    if (const char *e = std::getenv("GT4_CAPTURE_FROM")) g_capture_from = static_cast<uint32_t>(std::strtoul(e, nullptr, 0));
    if (const char *e = std::getenv("GT4_CAPTURE_FRAMES")) g_capture_frames = static_cast<uint32_t>(std::strtoul(e, nullptr, 0));
    g_capture = fopen(path, "wb");
    if (!g_capture) log_error("link: cannot write the capture %s", path);
    else log_info("link: capturing the graphics stream to %s", path);
    return g_capture != nullptr;
}

bool link_active() { return g_hdr || g_capture; }
bool link_connected() { return g_hdr != nullptr; }

void link_send(uint32_t type, const void *payload, uint32_t size, const void *extra, uint32_t extra_size) {
    if (!g_hdr && !g_capture) return;
    // Runtime code (KernelScope): the frame's interrupts must not freeze this thread while it holds the send mutex.
    // Their handlers send records too (VSYNC, GFX), under the kernel lock: frozen here, it would never release the
    // mutex and every guest thread would stop (the F7 freeze: a send waiting for ring room was preempted after
    // 50 ms). Held interrupts are delivered as it leaves, after the mutex is released.
    KernelScope ks;
    std::lock_guard<std::mutex> lock(g_send_mutex);
    Gt4LinkRecord rec{type, size + extra_size, ++g_seq};
    const uint32_t total = static_cast<uint32_t>((sizeof rec + size + extra_size + 15) & ~15u);
    static const uint8_t zeros[16] = {};
    if (g_capture && link_capture_window()) {
        if (!g_window_started) {  // the window opens: the remembered records first
            g_window_started = true;
            for (const auto &r : g_sticky) {
                uint32_t t;
                std::memcpy(&t, r.data(), 4);
                const uint32_t n = static_cast<uint32_t>(r.size() - 4);
                Gt4LinkRecord sr{t, n, 0};
                fwrite(&sr, sizeof sr, 1, g_capture);
                fwrite(r.data() + 4, 1, n, g_capture);
                fwrite(zeros, 1, ((sizeof sr + n + 15) & ~15u) - sizeof sr - n, g_capture);
            }
        }
        fwrite(&rec, sizeof rec, 1, g_capture);
        if (size) fwrite(payload, 1, size, g_capture);
        if (extra_size) fwrite(extra, 1, extra_size, g_capture);
        fwrite(zeros, 1, total - sizeof rec - size - extra_size, g_capture);
    }
    if (!g_hdr) return;
    const uint32_t ring = g_hdr->g2h_size;
    if (total > ring / 2) {
        log_warn("link: record of %u bytes dropped (ring %u)", total, ring);
        return;
    }
    uint64_t head = load_index(&g_hdr->g2h.head);
    // Wait for room (the host drains continuously). Not forever: some records are sent while the game holds its
    // kernel lock (GFX records from the frame callbacks), and an endless wait there stops every guest thread, VBlanks
    // included (seen with a host throttled behind the game). After a second the record is dropped, loudly.
    if (head + total - load_index(&g_hdr->g2h.tail) > ring) {
        const DWORD since = GetTickCount();
        while (head + total - load_index(&g_hdr->g2h.tail) > ring) {
            Sleep(1);
            if (GetTickCount() - since >= 1000) {
                static uint32_t dropped = 0;
                log_warn("link: the host drained nothing for 1 s; a record of type %u (%u bytes) DROPPED (%u so far)", type,
                         total, ++dropped);
                return;
            }
        }
    }
    if (type == GT4_REC_VSYNC && size >= 8) {
        uint32_t count;
        std::memcpy(&count, static_cast<const uint8_t *>(payload) + 4, 4);
        g_hdr->frame = count;
    }
    ring_write(g_g2h, ring, head, &rec, sizeof rec);
    if (size) ring_write(g_g2h, ring, head + sizeof rec, payload, size);
    if (extra_size) ring_write(g_g2h, ring, head + sizeof rec + size, extra, extra_size);
    MemoryBarrier();
    store_index(&g_hdr->g2h.head, head + total);
}

void link_poll() {
    if (!g_hdr) return;
    KernelScope ks;   // the interrupt handlers poll too (see link_send)
    std::lock_guard<std::recursive_mutex> poll_lock(g_poll_mutex);
    // the game's CPU time for the host's perf lines, every 100 ms at most (Wine's GetProcessTimes on another
    // process gives the caller's own, so the host cannot read it)
    static DWORD cpu_at = 0;
    if (const DWORD now = GetTickCount(); now - cpu_at >= 100) {
        cpu_at = now;
        FILETIME created, exited, kernel, user;
        if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
            const uint64_t t = ((uint64_t(kernel.dwHighDateTime) << 32) | kernel.dwLowDateTime) +
                               ((uint64_t(user.dwHighDateTime) << 32) | user.dwLowDateTime);
            g_hdr->game_cpu_ms = static_cast<uint32_t>(t / 10000);
        }
    }
    const uint32_t ring = g_hdr->h2g_size;
    uint64_t tail = load_index(&g_hdr->h2g.tail);
    const uint64_t head = load_index(&g_hdr->h2g.head);
    MemoryBarrier();
    while (tail + sizeof(Gt4LinkRecord) <= head) {
        Gt4LinkRecord rec;
        ring_read(g_h2g, ring, tail, &rec, sizeof rec);
        if (tail + sizeof rec + rec.size > head) break;  // not all written yet
        // Records of any size (GS_IMAGE carries image data). Reserved once, up to a photo's 640 x 480 x 24-bit read:
        // this runs in the frame's delivery too, where nothing may allocate (a guest thread frozen inside the heap).
        static std::vector<uint8_t> buf = [] {
            std::vector<uint8_t> v;
            v.reserve(1u << 20);
            return v;
        }();
        buf.resize(rec.size ? rec.size : 1);
        uint8_t *payload = buf.data();
        const uint32_t n = rec.size;
        ring_read(g_h2g, ring, tail + sizeof rec, payload, n);
        if (rec.type == GT4_REC_PAD && n >= 26) {
            uint32_t port, connected;
            std::memcpy(&port, payload, 4);
            std::memcpy(&connected, payload + 4, 4);
            if (port < 2) {
                std::lock_guard<std::mutex> lock(g_pad_mutex);
                std::memcpy(g_pad[port], payload + 8, 18);
                g_pad_valid[port] = connected != 0;
            }
        } else if (rec.type == GT4_REC_GS_IMAGE && n >= 8) {
            uint32_t id, qwc;
            std::memcpy(&id, payload, 4);
            std::memcpy(&qwc, payload + 4, 4);
            if (id == g_image_id && g_image_dst) {
                const uint32_t have = (n - 8) / 16;
                const uint32_t copy = (qwc < g_image_qwc ? qwc : g_image_qwc);
                const uint32_t got = copy < have ? copy : have;
                std::memcpy(g_image_dst, payload + 8, got * 16);
                g_image_done = true;
            } else {
                for (AsyncRead &a : g_async) {
                    if (!a.dst || a.id != id) continue;
                    const uint32_t have = (n - 8) / 16;
                    const uint32_t copy = (qwc < a.qwc ? qwc : a.qwc);
                    const uint32_t got = copy < have ? copy : have;
                    async_land(a.dst, payload + 8, size_t(got) * 16);
                    async_landed_after(g_hdr->frame - a.frame);
                    a = AsyncRead();
                    g_async_landed = true;
                }
            }
        } else if (rec.type == GT4_REC_HOST_CMD && n >= 4) {
            uint32_t cmd;
            std::memcpy(&cmd, payload, 4);
            if (cmd == 1) {
                log_info("link: the host asked to quit");
                g_hdr->game_state = 2;
                ExitProcess(0);
            }
            if (cmd == 2 || cmd == 3) kernel_set_paused(cmd == 2);
            if (cmd == 5) frame_forget_sent_contents();   // the host lost a texture content (runtime/frame.cpp)
            if (cmd == 4 && n >= 12) {
                uint32_t id, value;
                std::memcpy(&id, payload + 4, 4);
                std::memcpy(&value, payload + 8, 4);
                set_game_option(id, value);
            }
        }
        tail += (sizeof rec + rec.size + 15) & ~15ull;
    }
    store_index(&g_hdr->h2g.tail, tail);
}

bool link_read_image(uint32_t qwc, void *dst) {
    std::memset(dst, 0, static_cast<size_t>(qwc) * 16);
    if (!g_hdr) return false;
    {
        KernelScope ks;
        std::lock_guard<std::recursive_mutex> poll_lock(g_poll_mutex);
        g_image_id = ++g_image_next ? g_image_next : ++g_image_next;
        g_image_dst = static_cast<uint8_t *>(dst);
        g_image_qwc = qwc;
        g_image_done = false;
    }
    const uint32_t req[2] = {g_image_id, qwc};
    const DWORD since = GetTickCount();
    link_send(GT4_REC_GS_READ, req, sizeof req);
    bool done = false;
    for (int waited = 0; waited < 2000 && !done; ++waited) {
        link_poll();
        {
            KernelScope ks;
            std::lock_guard<std::recursive_mutex> poll_lock(g_poll_mutex);
            done = g_image_done;
        }
        if (!done) Sleep(1);
    }
    KernelScope ks;
    std::lock_guard<std::recursive_mutex> poll_lock(g_poll_mutex);
    g_image_id = 0;
    g_image_dst = nullptr;
    // Reads, and the slow ones (the host answers after drawing everything queued before the read).
    static uint32_t reads = 0, slow = 0;
    ++reads;
    const DWORD took = GetTickCount() - since;
    if (!done) log_warn("link: no GS_IMAGE for a %u-qword download after %lu ms (read %u)", qwc, static_cast<unsigned long>(took), reads);
    else if (took >= 50 && (++slow <= 20 || slow % 100 == 0))
        log_warn("link: a %u-qword GS read took %lu ms (read %u, %u slow so far)", qwc, static_cast<unsigned long>(took), reads, slow);
    return done;
}

bool link_read_image_async(uint32_t qwc, void *dst) {
    if (!g_hdr) {
        std::memset(dst, 0, static_cast<size_t>(qwc) * 16);
        return false;
    }
    static uint32_t reads = 0, untracked = 0, lost = 0;
    ++reads;
    if (qwc > kAsyncMaxQwc) {   // (more than the landing buffers hold: waited for)
        static bool told = false;
        if (!told) log_warn("link: a %u-qword asynchronous read (over %u) is waited for", qwc, kAsyncMaxQwc);
        told = true;
        return link_read_image(qwc, dst);
    }
    link_poll();   // (what has landed so far frees its slot)
    // The first one is read as the game asks (nothing landed yet to read the frame after).
    bool landed;
    {
        KernelScope ks;
        std::lock_guard<std::recursive_mutex> poll_lock(g_poll_mutex);
        landed = g_async_landed;
    }
    if (!landed) {
        const bool done = link_read_image(qwc, dst);
        KernelScope ks;
        std::lock_guard<std::recursive_mutex> poll_lock(g_poll_mutex);
        g_async_landed = done;
        return done;
    }
    // Never waited for, as on the PS2: a read that finds every slot taken (a host far behind) is sent all the same and
    // its pixels not kept (the game goes on with the newest that landed); one not answered within kLostFrames VBlanks
    // frees its slot (an answer lost: never seen, but the game must not wait on it).
    uint32_t req[2] = {0, qwc};
    {
        KernelScope ks;
        std::lock_guard<std::recursive_mutex> poll_lock(g_poll_mutex);
        constexpr uint32_t kLostFrames = 60;
        for (AsyncRead &a : g_async) {
            if (!a.dst || g_hdr->frame - a.frame <= kLostFrames) continue;
            ++lost;
            log_warn("link: no GS_IMAGE for asynchronous read id %u after %u VBlanks: its slot freed (%u so far)", a.id,
                     g_hdr->frame - a.frame, lost);
            a = AsyncRead();
        }
        req[0] = ++g_image_next ? g_image_next : ++g_image_next;
        bool kept = false;
        for (int k = 0; k < async_depth() && !kept; ++k) {
            AsyncRead &a = g_async[k];
            if (a.dst) continue;
            a.id = req[0];
            a.dst = static_cast<uint8_t *>(dst);
            a.qwc = qwc;
            a.frame = g_hdr->frame;
            kept = true;
        }
        if (!kept && (++untracked <= 10 || untracked % 100 == 0))
            log_info("link: asynchronous read %u sent untracked, %d in flight (the host is behind; %u so far)", reads,
                     async_in_flight(), untracked);
    }
    link_send(GT4_REC_GS_READ, req, sizeof req);
    return true;
}

void link_async_deliver(void *dst) {
    if (!g_hdr) return;
    KernelScope ks;
    std::lock_guard<std::recursive_mutex> poll_lock(g_poll_mutex);
    for (Landed &l : g_landed) {
        if (!l.fresh || l.dst != dst) continue;
        if (g_hdr->frame - l.frame <= kLandedFrames) {
            std::memcpy(dst, l.bytes, l.size);
            ++g_async_delivered;
        } else {
            ++g_async_expired;
        }
        l.fresh = false;
    }
}

void link_wait_for_host() {
    // Frame skipping on the host keeps it within a frame or two; this only catches a host that cannot keep up at
    // all. Not in an interrupt (the kernel lock is held there), and never for long: a host that draws nothing (a
    // minimised window) is not waited for again until it draws. The way from a VSYNC to the frame drawn (the link,
    // the stream, the queue, a present) takes 2 to 4 VBlanks, up to 7 with a GPU at 18 ms a frame: 10 is about six
    // frames past that.
    constexpr uint32_t kAhead = 10;
    static const bool on = [] { const char *e = std::getenv("GT4_HOST_WAIT"); return !(e && *e == '0'); }();   // 0: never
    if (!on || !g_hdr || kernel_in_interrupt()) return;
    static uint32_t stalled_at = 0;   // the host's frame when a whole wait saw it draw nothing
    const uint32_t drawn = g_hdr->host_frame_drawn;
    if (!drawn || drawn == stalled_at || g_hdr->frame - drawn <= kAhead) return;
    const DWORD since = GetTickCount();
    while (g_hdr->frame - g_hdr->host_frame_drawn > kAhead) {
        if (GetTickCount() - since >= 250) {
            if (g_hdr->host_frame_drawn == drawn) stalled_at = drawn;
            break;
        }
        KernelScope ks;   // a safe point: the frame's interrupts are delivered here as it leaves, not by freezing it
        Sleep(1);
    }
    g_hdr->game_host_waits = g_hdr->game_host_waits + 1;
    g_hdr->game_host_wait_ms = g_hdr->game_host_wait_ms + (GetTickCount() - since);
}

bool link_pad(int port, uint8_t out[18]) {
    KernelScope ks;   // link_poll takes this mutex in the interrupt handlers (see link_send)
    std::lock_guard<std::mutex> lock(g_pad_mutex);
    if (port < 0 || port > 1 || !g_pad_valid[port]) return false;
    std::memcpy(out, g_pad[port], 18);
    return true;
}

}  // namespace gt4::rt
