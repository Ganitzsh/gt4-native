// mapped_read.h. PLAN.md D5's profile (2026-11-10): the unpacks' sources outside the guest's RAM, game arrays on a
// native stack, asked the system for each (VirtualQuery), about 250 us a frame in a race on the desktop, more under
// Wine and FEX, where a system call costs more.
#include "mapped_read.h"

#include "guest.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstring>

namespace gt4::rt {

namespace {
std::atomic<uint64_t> g_stack{0}, g_asked{0};
}  // namespace

void read_mapped_into(uint8_t *out, const uint8_t *src, size_t bytes) {
    const uintptr_t a = reinterpret_cast<uintptr_t>(src);
    if (!bytes) return;
    if (in_guest_ram(a, bytes)) {
        std::memcpy(out, src, bytes);
        return;
    }
    // The calling thread's stack as its TEB has it now (read each call: a fiber switch changes it): from StackLimit,
    // its lowest committed page, to StackBase, its top, committed and readable throughout. A range wholly inside is
    // what VirtualQuery would have let through whole; a range that runs past the top asks, as before.
    const NT_TIB *tib = reinterpret_cast<const NT_TIB *>(NtCurrentTeb());
    const uintptr_t low = reinterpret_cast<uintptr_t>(tib->StackLimit);
    const uintptr_t top = reinterpret_cast<uintptr_t>(tib->StackBase);
    if (a >= low && a <= top && bytes <= top - a) {
        g_stack.fetch_add(1, std::memory_order_relaxed);
        std::memcpy(out, src, bytes);
        return;
    }
    g_asked.fetch_add(1, std::memory_order_relaxed);
    size_t done = 0;
    while (done < bytes) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(src + done, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT ||
            (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
            break;
        const size_t avail = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize - (a + done);
        const size_t take = std::min(avail, bytes - done);
        std::memcpy(out + done, src + done, take);
        done += take;
    }
}

MappedReadCounts mapped_read_counts() { return {g_stack.load(), g_asked.load()}; }

}  // namespace gt4::rt
