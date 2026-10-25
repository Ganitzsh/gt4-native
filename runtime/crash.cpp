// Crash reports: an exception handler installed before main (a static constructor) that prints the
// exception, the faulting address with the native function containing it, and the stack's return
// addresses that fall in native game code.
#include "image.h"
#include "log.h"

#include <windows.h>

#include <cstdio>

namespace {

void describe(uintptr_t pc, char *out, size_t n) {
    uint32_t guest = 0;
    const char *name = gt4::rt::native_name_for(pc, &guest);
    if (*name)
        snprintf(out, n, "%08x (%s+0x%x, PS2 %08x)", static_cast<unsigned>(pc), name,
                 static_cast<unsigned>(pc - reinterpret_cast<uintptr_t>(nullptr)), guest);
    else
        snprintf(out, n, "%08x", static_cast<unsigned>(pc));
}

LONG WINAPI on_crash(EXCEPTION_POINTERS *ep) {
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code == 0x406D1388 || code == DBG_PRINTEXCEPTION_C || code == 0xE06D7363) return EXCEPTION_CONTINUE_SEARCH;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_STACK_OVERFLOW && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_PRIV_INSTRUCTION && code != 0xC0000409)
        return EXCEPTION_CONTINUE_SEARCH;
    static volatile LONG once = 0;
    if (InterlockedExchange(&once, 1)) return EXCEPTION_CONTINUE_SEARCH;
    const CONTEXT *c = ep->ContextRecord;
    char where[256];
    describe(c->Eip, where, sizeof where);
    fprintf(stderr, "CRASH: exception %08lx at %s", code, where);
    if (code == EXCEPTION_ACCESS_VIOLATION)
        fprintf(stderr, ", %s address %08lx", ep->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading",
                static_cast<unsigned long>(ep->ExceptionRecord->ExceptionInformation[1]));
    fprintf(stderr, "\n  eax %08lx ebx %08lx ecx %08lx edx %08lx esi %08lx edi %08lx ebp %08lx esp %08lx\n", c->Eax,
            c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
    // return addresses on the stack that point into the program's code
    const auto *sp = reinterpret_cast<const uint32_t *>(c->Esp);
    int shown = 0;
    MEMORY_BASIC_INFORMATION mi;
    for (int i = 0; i < 4096 && shown < 24; ++i) {
        if (!VirtualQuery(sp + i, &mi, sizeof mi) || mi.State != MEM_COMMIT) break;
        const uint32_t v = sp[i];
        if (v >= 0x02000000 && v < 0x04000000) {
            uint32_t g = 0;
            const char *name = gt4::rt::native_name_for(v, &g);
            if (*name) {
                fprintf(stderr, "  [esp+%04x] %08x in %s (PS2 %08x)\n", i * 4, v, name, g);
                ++shown;
            }
        }
    }
    fflush(stderr);
    gt4::rt::log_error("crashed: exception %08lx at %s", code, where);
    TerminateProcess(GetCurrentProcess(), 6);
    return EXCEPTION_CONTINUE_SEARCH;
}

struct Install {
    Install() {
        AddVectoredExceptionHandler(1, on_crash);
        fprintf(stderr, "gt4_game: starting\n");
    }
} g_install __attribute__((init_priority(101)));

}  // namespace
