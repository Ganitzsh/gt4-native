// GT4_HW_AUDIT=1 (checking the port): logs every function that still loads or stores a PS2 hardware register (the
// blocks of guest.h kHwBlocks: EE registers, VU memory, the GS privileged registers), once per function, register
// and direction, with the game functions found on its stack. The blocks are mapped without access; each access
// faults, is noted, and is single-stepped with its page opened for that one instruction. Accesses of other threads
// in that window pass unseen, so a run lists what it reached at least once.
//
// The fault handler takes no lock and allocates nothing: the kernel's interrupt thread suspends the running game
// thread wherever it is (kernel.cpp interrupt_now) and runs handlers that may fault in turn. It claims a slot in a
// fixed table; a thread of its own writes the new slots to the log.
#include "guest.h"
#include "image.h"
#include "log.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

using namespace gt4::rt;

namespace {

struct Hit {
    uintptr_t eip;            // the access (a runtime function when no game function is found)
    const char *fn;           // its game function, or null
    const char *callers[4];   // game functions found on the stack
    uint32_t addr;
    bool write;
    volatile LONG ready;
};
constexpr LONG kHits = 1024;
Hit g_hits[kHits];
volatile LONG g_used = 0;
thread_local void *t_page = nullptr;   // the page opened for the instruction being stepped

const char *name_of(uintptr_t pc) {
    uint32_t guest = 0;
    const char *n = native_name_for(pc, &guest);
    return n && *n ? n : nullptr;
}

const HwBlock *block_of(uintptr_t at) {
    for (const HwBlock &b : kHwBlocks)
        if (at >= b.base && at < b.base + b.size) return &b;
    return nullptr;
}

void note(const CONTEXT *c, uint32_t addr, bool write) {
    const char *fn = name_of(c->Eip);
    const LONG used = g_used < kHits ? g_used : kHits;
    for (LONG k = 0; k < used; ++k) {
        const Hit &h = g_hits[k];
        if (h.addr == addr && h.write == write && (fn ? h.fn == fn : !h.fn && h.eip == c->Eip)) return;
    }
    const LONG k = InterlockedIncrement(&g_used) - 1;
    if (k >= kHits) return;
    Hit &h = g_hits[k];
    h.eip = c->Eip;
    h.fn = fn;
    h.addr = addr;
    h.write = write;
    const uint32_t *sp = reinterpret_cast<const uint32_t *>(c->Esp);
    for (int s = 0, found = 0; s < 96 && found < 4; ++s)
        if (const char *n = name_of(sp[s])) h.callers[found++] = n;
    InterlockedExchange(&h.ready, 1);
}

LONG WINAPI on_fault(EXCEPTION_POINTERS *ep) {
    const EXCEPTION_RECORD *er = ep->ExceptionRecord;
    CONTEXT *c = ep->ContextRecord;
    if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2) {
        const uintptr_t at = er->ExceptionInformation[1];
        if (!block_of(at)) return EXCEPTION_CONTINUE_SEARCH;
        note(c, static_cast<uint32_t>(at) & ~7u, er->ExceptionInformation[0] == 1);
        t_page = reinterpret_cast<void *>(at & ~uintptr_t(0xFFF));
        DWORD old;
        VirtualProtect(t_page, 4096, PAGE_READWRITE, &old);
        c->EFlags |= 0x100;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (er->ExceptionCode == EXCEPTION_SINGLE_STEP && t_page) {
        DWORD old;
        VirtualProtect(t_page, 4096, PAGE_NOACCESS, &old);
        t_page = nullptr;
        c->EFlags &= ~0x100u;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

DWORD WINAPI writer(void *) {
    LONG next = 0;
    for (;;) {
        while (next < kHits && next < g_used && g_hits[next].ready) {
            const Hit &h = g_hits[next++];
            const HwBlock *b = block_of(h.addr);
            char where[32];
            std::snprintf(where, sizeof where, "runtime %08lx", static_cast<unsigned long>(h.eip));
            log_info("hw audit: %s %s %08x (%s); callers: %s %s %s %s", h.fn ? h.fn : where, h.write ? "writes" : "reads", h.addr,
                     b ? b->name : "?", h.callers[0] ? h.callers[0] : "-", h.callers[1] ? h.callers[1] : "", h.callers[2] ? h.callers[2] : "",
                     h.callers[3] ? h.callers[3] : "");
        }
        Sleep(50);
    }
}

}  // namespace

namespace gt4::rt {

void hw_audit_init() {
    const char *e = getenv("GT4_HW_AUDIT");
    if (!e || !*e || *e == '0') return;
    AddVectoredExceptionHandler(1, on_fault);
    CreateThread(nullptr, 1u << 16, writer, nullptr, 0, nullptr);
    for (const HwBlock &b : kHwBlocks) {
        DWORD old;
        VirtualProtect(guest_ptr(b.base), b.size, PAGE_NOACCESS, &old);
    }
    log_info("hw audit: every access to the hardware blocks is logged once per function and register");
}

}  // namespace gt4::rt
