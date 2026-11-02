// GT4_HEAP_LOG=SECONDS (debugging): every SECONDS, the game's heap (the pool 008744D0 that malloc and the tagged
// allocator draw from, 008A215C-01FF8000) walked block by block: bytes in use, free, the largest free block and the
// block counts. A block is 16 bytes of header {prev, end, w8, wC} then its data; a used block has w8 = wC = -1
// (005725A8), a free one links its bin's list through them. Read while the game runs: a figure may be off by a
// block being split or merged at that moment.
#include "guest.h"
#include "log.h"

#include <windows.h>

#include <cstdint>
#include <cstdlib>

namespace gt4::rt {
namespace {

constexpr uint32_t kPool = 0x008744D0;

DWORD WINAPI heap_thread(void *p) {
    const DWORD period = static_cast<DWORD>(reinterpret_cast<uintptr_t>(p)) * 1000;
    for (;;) {
        Sleep(period);
        const uint32_t start = guest_ref<uint32_t>(kPool + 8), end = guest_ref<uint32_t>(kPool + 4);
        if (start < 0x11000 || end > 0x02000000 || start >= end) continue;
        uint64_t used = 0, free = 0;
        uint32_t largest = 0, used_blocks = 0, free_blocks = 0;
        for (uint32_t b = start; b < end;) {
            const uint32_t next = guest_ref<uint32_t>(b + 4);
            if (next <= b || next > end) break;
            const uint32_t size = next - b;
            if (guest_ref<int32_t>(b + 8) == -1 && guest_ref<int32_t>(b + 12) == -1) {
                used += size;
                ++used_blocks;
            } else {
                free += size;
                ++free_blocks;
                if (size > largest) largest = size;
            }
            b = next;
        }
        log_info("heap: %llu KiB used in %u blocks, %llu KiB free in %u blocks, largest free %u KiB",
                 static_cast<unsigned long long>(used >> 10), used_blocks, static_cast<unsigned long long>(free >> 10),
                 free_blocks, largest >> 10);
        // short of room: the largest blocks in use, with their first words (what they hold)
        static int listed = 0;
        if (free < (4u << 20) && listed < 3) {
            ++listed;
            uint32_t top[15] = {}, top_size[15] = {};
            for (uint32_t b = start; b < end;) {
                const uint32_t next = guest_ref<uint32_t>(b + 4);
                if (next <= b || next > end) break;
                if (guest_ref<int32_t>(b + 8) == -1 && guest_ref<int32_t>(b + 12) == -1) {
                    const uint32_t size = next - b;
                    for (int k = 0; k < 15; ++k)
                        if (size > top_size[k]) {
                            for (int m = 14; m > k; --m) top[m] = top[m - 1], top_size[m] = top_size[m - 1];
                            top[k] = b;
                            top_size[k] = size;
                            break;
                        }
                }
                b = next;
            }
            for (int k = 0; k < 15 && top[k]; ++k) {
                const uint32_t d = top[k] + 16;
                log_info("heap:   %6u KiB at %08x: %08x %08x %08x %08x", top_size[k] >> 10, d, guest_ref<uint32_t>(d),
                         guest_ref<uint32_t>(d + 4), guest_ref<uint32_t>(d + 8), guest_ref<uint32_t>(d + 12));
            }
        }
    }
}

}  // namespace

void heap_log_start() {
    if (const char *s = getenv("GT4_HEAP_LOG"); s && atoi(s) > 0)
        CreateThread(nullptr, 1u << 16, heap_thread, reinterpret_cast<void *>(static_cast<uintptr_t>(atoi(s))), 0, nullptr);
}

}  // namespace gt4::rt
