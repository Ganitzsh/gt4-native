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
        code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_PRIV_INSTRUCTION && code != 0xC0000409 &&
        code != EXCEPTION_FLT_DIVIDE_BY_ZERO && code != EXCEPTION_FLT_INVALID_OPERATION && code != EXCEPTION_FLT_OVERFLOW &&
        code != EXCEPTION_FLT_STACK_CHECK)   // the float ones only with GT4_FPU_TRAP (kernel.cpp)
        return EXCEPTION_CONTINUE_SEARCH;
    char where[256];
    if (code >= EXCEPTION_FLT_DENORMAL_OPERAND && code <= EXCEPTION_FLT_UNDERFLOW) {
        // GT4_FPU_TRAP: an IEEE infinity or NaN where the EE would have clamped. Each instruction that made one is
        // reported once (the x87 records it), then the program goes on with the IEEE result.
        __asm__ volatile("fnclex");   // the pending exception would fault again in the logging below
        CONTEXT *w = ep->ContextRecord;
        const DWORD at = w->FloatSave.ErrorOffset;
        static DWORD seen[1024];
        static volatile LONG count = 0;
        bool known = false;
        for (LONG i = 0; i < count && i < 1024; ++i) known |= seen[i] == at;
        if (!known) {
            const LONG slot = InterlockedIncrement(&count) - 1;
            if (slot < 1024) seen[slot] = at;
            describe(at, where, sizeof where);
            char caller[256] = "?";
            // the caller of the faulting function, from its frame
            const DWORD *frame = reinterpret_cast<const DWORD *>(w->Ebp);
            if (!IsBadReadPtr(frame, 8)) describe(frame[1], caller, sizeof caller);
            gt4::rt::log_error("x87 %s at %08lx (%s), called from %s",
                               code == EXCEPTION_FLT_DIVIDE_BY_ZERO ? "divide by zero" : code == EXCEPTION_FLT_OVERFLOW ? "overflow"
                                                                                                                : "invalid",
                               static_cast<unsigned long>(at), where, caller);
        }
        w->FloatSave.StatusWord &= ~0x80FFu;   // exception flags, error summary and busy
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    static volatile LONG once = 0;
    if (InterlockedExchange(&once, 1)) return EXCEPTION_CONTINUE_SEARCH;
    const CONTEXT *c = ep->ContextRecord;
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
    // the stack words around esp (a smashed frame shows here)
    {
        MEMORY_BASIC_INFORMATION mi;
        const auto *sp = reinterpret_cast<const uint32_t *>(c->Esp);
        if (VirtualQuery(sp - 8, &mi, sizeof mi) && mi.State == MEM_COMMIT &&
            reinterpret_cast<uintptr_t>(sp + 24) <= reinterpret_cast<uintptr_t>(mi.BaseAddress) + mi.RegionSize) {
            for (int row = -8; row < 24; row += 8) {
                fprintf(stderr, "  esp%+04x:", row * 4);
                for (int k = 0; k < 8; ++k) fprintf(stderr, " %08x", sp[row + k]);
                fprintf(stderr, "\n");
            }
        }
    }
    // the frame-pointer chain ([ebp] = caller's ebp, [ebp+4] = return address): game code keeps
    // frame pointers, so this is the call chain as far as it goes through such frames
    {
        MEMORY_BASIC_INFORMATION mi;
        uint32_t bp = c->Ebp;
        for (int depth = 0; depth < 32; ++depth) {
            const auto *f = reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(bp));
            if (bp < 0x10000 || (bp & 3) || !VirtualQuery(f, &mi, sizeof mi) || mi.State != MEM_COMMIT ||
                reinterpret_cast<uintptr_t>(f + 6) > reinterpret_cast<uintptr_t>(mi.BaseAddress) + mi.RegionSize)
                break;
            const uint32_t ret = f[1];
            uint32_t g = 0;
            const char *name = gt4::rt::native_name_for(ret, &g);
            fprintf(stderr, "  frame %2d: ebp %08x ret %08x in %s (PS2 %08x) args %08x %08x %08x %08x\n", depth, bp, ret,
                    name, g, f[2], f[3], f[4], f[5]);
            if (f[0] <= bp) break;  // frames go up the stack
            bp = f[0];
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
    // GT4_SYMDUMP: the script engine's symbol table (MADHOC::SymbolMap, a map<String, int> at
    // 0x0061A100 + 0x30), as "id name" lines, to name the symbol IDs a script lookup used
    if (getenv("GT4_SYMDUMP")) {
        const auto rd = [](uint32_t a) { return *reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(a)); };
        uint32_t header = 0;
        for (uint32_t off = 0x30; off < 0x40 && !header; off += 4) {  // the tree's header node
            const uint32_t h = rd(0x0061A100 + off);
            if (h > 0x10000 && h < 0x02000000 && rd(h + 4) > 0x10000 && rd(h + 4) < 0x02000000 && rd(rd(h + 4) + 4) == h)
                header = rd(h) == 1 ? rd(h + 4) : h;  // the header is red (0); a black node is the root
        }
        fprintf(stderr, "symbol map header %08x\n", header);
        // in order from the leftmost node (header->left), as the tree's iterator steps
        uint32_t n = header ? rd(header + 8) : 0;
        for (int count = 0; n && n != header && count < 20000; ++count) {
            const uint32_t text = rd(n + 0x10);
            fprintf(stderr, "  sym %5d %s\n", static_cast<int>(rd(n + 0x14)), reinterpret_cast<const char *>(static_cast<uintptr_t>(text)));
            if (rd(n + 12)) {
                n = rd(n + 12);
                while (rd(n + 8)) n = rd(n + 8);
            } else {
                uint32_t up = rd(n + 4);
                while (n == rd(up + 12)) {
                    n = up;
                    up = rd(up + 4);
                }
                if (rd(n + 12) != up) n = up;
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
