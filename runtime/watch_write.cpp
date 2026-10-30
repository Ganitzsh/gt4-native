// GT4_WATCH_WRITE=ADDR[,N] (debugging): logs the game functions that write the 16 bytes at guest address ADDR
// (hex), the first N times (default 20): the page is write-protected, each store to it faults, the writer is named
// (image.cpp native_name_for, with the return addresses near the stack top), and the store is single-stepped. Other
// stores to the page pass silently. Protecting a page of guest RAM detaches the main view from the mirrors at
// 0x20000000 / 0x30000000 for that page (gs_writers.cpp), so a run with a watch is for finding writers only.
#include "guest.h"
#include "image.h"
#include "log.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace gt4::rt;

namespace {

uint32_t g_addr = 0;
long g_left = 0;
void *g_page = nullptr;
DWORD g_old = 0;
thread_local bool t_stepping = false;

const char *name_of(uintptr_t pc) {
    uint32_t guest = 0;
    const char *n = native_name_for(pc, &guest);
    return n && *n ? n : "?";
}

LONG WINAPI on_fault(EXCEPTION_POINTERS *ep) {
    const EXCEPTION_RECORD *er = ep->ExceptionRecord;
    CONTEXT *c = ep->ContextRecord;
    if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2 && er->ExceptionInformation[0] == 1) {
        const uintptr_t at = er->ExceptionInformation[1];
        if ((at & ~uintptr_t(0xFFF)) != reinterpret_cast<uintptr_t>(g_page)) return EXCEPTION_CONTINUE_SEARCH;
        DWORD old;
        VirtualProtect(g_page, 4096, g_old, &old);
        if (at + 8 > g_addr && at < g_addr + 16 && g_left > 0) {
            --g_left;
            const uint32_t *sp = reinterpret_cast<const uint32_t *>(c->Esp);
            char callers[512] = "";
            size_t used = 0;
            for (int k = 0; k < 64 && used < sizeof callers - 64; ++k) {
                uint32_t guest = 0;
                const char *n = native_name_for(sp[k], &guest);
                if (n && *n) used += static_cast<size_t>(snprintf(callers + used, sizeof callers - used, " %s", n));
            }
            log_info("watch: %08x written (%08x) by %s; stack:%s", g_addr, static_cast<uint32_t>(at), name_of(c->Eip), callers);
        }
        c->EFlags |= 0x100;
        t_stepping = true;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (er->ExceptionCode == EXCEPTION_SINGLE_STEP && t_stepping) {
        t_stepping = false;
        DWORD old;
        if (g_left > 0) VirtualProtect(g_page, 4096, PAGE_READONLY, &old);
        c->EFlags &= ~0x100u;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

namespace gt4::rt {

void watch_write_init() {
    const char *e = getenv("GT4_WATCH_WRITE");
    if (!e || !*e) return;
    g_addr = static_cast<uint32_t>(strtoul(e, nullptr, 16));
    const char *comma = strchr(e, ',');
    g_left = comma ? atol(comma + 1) : 20;
    g_page = guest_ptr(g_addr & ~0xFFFu);
    MEMORY_BASIC_INFORMATION mbi;
    VirtualQuery(g_page, &mbi, sizeof mbi);
    g_old = mbi.Protect;
    AddVectoredExceptionHandler(1, on_fault);
    DWORD old;
    VirtualProtect(g_page, 4096, PAGE_READONLY, &old);
    log_info("watch: writes to %08x (%ld times)", g_addr, g_left);
}

}  // namespace gt4::rt
