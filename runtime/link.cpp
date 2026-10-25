#include "link.h"

#include "gt4/link/protocol.h"
#include "log.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <mutex>

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

}  // namespace

bool link_open(const char *name) {
    HANDLE h = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name);
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
    g_g2h = static_cast<uint8_t *>(view) + g_hdr->g2h_offset;
    g_h2g = static_cast<uint8_t *>(view) + g_hdr->h2g_offset;
    g_hdr->game_pid = GetCurrentProcessId();
    g_hdr->game_state = 1;
    log_info("link: connected to %s", name);
    return true;
}

bool link_capture(const char *path) {
    g_capture = fopen(path, "wb");
    if (!g_capture) log_error("link: cannot write the capture %s", path);
    else log_info("link: capturing the graphics stream to %s", path);
    return g_capture != nullptr;
}

bool link_active() { return g_hdr || g_capture; }

void link_send(uint32_t type, const void *payload, uint32_t size, const void *extra, uint32_t extra_size) {
    if (!g_hdr && !g_capture) return;
    std::lock_guard<std::mutex> lock(g_send_mutex);
    Gt4LinkRecord rec{type, size + extra_size, ++g_seq};
    const uint32_t total = static_cast<uint32_t>((sizeof rec + size + extra_size + 15) & ~15u);
    static const uint8_t zeros[16] = {};
    if (g_capture) {
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
    uint64_t head = g_hdr->g2h.head;
    // wait for room (the host consumes continuously; give up after a second rather than hang)
    for (int spins = 0; head + total - g_hdr->g2h.tail > ring; ++spins) {
        if (spins > 1000) {
            log_warn("link: host not consuming; record dropped");
            return;
        }
        Sleep(1);
    }
    ring_write(g_g2h, ring, head, &rec, sizeof rec);
    if (size) ring_write(g_g2h, ring, head + sizeof rec, payload, size);
    if (extra_size) ring_write(g_g2h, ring, head + sizeof rec + size, extra, extra_size);
    MemoryBarrier();
    g_hdr->g2h.head = head + total;
}

void link_poll() {
    if (!g_hdr) return;
    const uint32_t ring = g_hdr->h2g_size;
    uint64_t tail = g_hdr->h2g.tail;
    const uint64_t head = g_hdr->h2g.head;
    MemoryBarrier();
    while (tail + sizeof(Gt4LinkRecord) <= head) {
        Gt4LinkRecord rec;
        ring_read(g_h2g, ring, tail, &rec, sizeof rec);
        uint8_t payload[256];
        const uint32_t n = rec.size < sizeof payload ? rec.size : sizeof payload;
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
        } else if (rec.type == GT4_REC_HOST_CMD && n >= 4) {
            uint32_t cmd;
            std::memcpy(&cmd, payload, 4);
            if (cmd == 1) {
                log_info("link: the host asked to quit");
                g_hdr->game_state = 2;
                ExitProcess(0);
            }
        }
        tail += (sizeof rec + rec.size + 15) & ~15ull;
    }
    g_hdr->h2g.tail = tail;
}

bool link_pad(int port, uint8_t out[18]) {
    std::lock_guard<std::mutex> lock(g_pad_mutex);
    if (port < 0 || port > 1 || !g_pad_valid[port]) return false;
    std::memcpy(out, g_pad[port], 18);
    return true;
}

}  // namespace gt4::rt
