// Crash reports: an exception handler installed before main (a static constructor) that prints the
// exception, the faulting address with the native function containing it, and the stack's return
// addresses that fall in native game code.
#include "image.h"
#include "log.h"

#include <windows.h>

#include <cstdio>

extern "C" {
extern const unsigned gt4_unresolved_base, gt4_unresolved_count;
extern const char *const gt4_unresolved_names[];
}

namespace {

const char *unresolved_name(uintptr_t a) {
    if (a < gt4_unresolved_base || a >= gt4_unresolved_base + 16 * gt4_unresolved_count) return nullptr;
    return gt4_unresolved_names[(a - gt4_unresolved_base) / 16];
}

void describe(uintptr_t pc, char *out, size_t n) {
    uint32_t guest = 0;
    if (const char *u = unresolved_name(pc)) {
        snprintf(out, n, "%08x (call of UNRESOLVED symbol %s)", static_cast<unsigned>(pc), u);
        return;
    }
    const char *name = gt4::rt::native_name_for(pc, &guest);
    if (*name)
        snprintf(out, n, "%08x (in %s, PS2 %08x)", static_cast<unsigned>(pc), name, guest);
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
    if (code == EXCEPTION_ACCESS_VIOLATION) {
        const uintptr_t fault = ep->ExceptionRecord->ExceptionInformation[1];
        fprintf(stderr, ", %s address %08lx", ep->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading",
                static_cast<unsigned long>(fault));
        if (const char *u = unresolved_name(fault)) fprintf(stderr, " = UNRESOLVED data symbol %s", u);
    }
    fprintf(stderr, "\n  eax %08lx ebx %08lx ecx %08lx edx %08lx esi %08lx edi %08lx ebp %08lx esp %08lx\n", c->Eax,
            c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
    // the crashing function's arguments ([ebp+8]...: gcc 2.9 keeps frame pointers) and what they point at
    {
        MEMORY_BASIC_INFORMATION mi;
        const auto *bp = reinterpret_cast<const uint32_t *>(c->Ebp);
        if (VirtualQuery(bp, &mi, sizeof mi) && mi.State == MEM_COMMIT) {
            fprintf(stderr, "  args:");
            for (int i = 0; i < 5; ++i) fprintf(stderr, " %08x", bp[2 + i]);
            fprintf(stderr, "\n");
            for (int i = 0; i < 5; ++i) {
                const auto *p = reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(bp[2 + i]));
                if (bp[2 + i] < 0x10000 || !VirtualQuery(p, &mi, sizeof mi) || mi.State != MEM_COMMIT ||
                    (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
                    continue;
                fprintf(stderr, "  *arg%d:", i);
                for (int k = 0; k < 12; ++k) fprintf(stderr, " %08x", p[k]);
                fprintf(stderr, "\n");
            }
        }
    }
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
    // GT4_DUMP=addr:len[,addr:len...]: guest memory to show with the report
    if (const char *d = getenv("GT4_DUMP")) {
        const char *p = d;
        while (*p) {
            char *end;
            const unsigned long addr = strtoul(p, &end, 16);
            unsigned long len = 64;
            if (*end == ':') len = strtoul(end + 1, &end, 0);
            for (unsigned long off = 0; off < len; off += 16) {
                fprintf(stderr, "  %08lx:", addr + off);
                for (unsigned long k = 0; k < 16 && off + k < len; k += 4)
                    fprintf(stderr, " %08x", *reinterpret_cast<const unsigned *>(addr + off + k));
                fprintf(stderr, "\n");
            }
            p = *end == ',' ? end + 1 : end;
            if (end == p && *p) break;
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
