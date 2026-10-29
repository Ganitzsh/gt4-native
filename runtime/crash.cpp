// Crash reports: an exception handler installed before main (a static constructor) that prints the
// exception, the faulting address with the native function containing it, and the stack's return
// addresses that fall in native game code.
#include "image.h"
#include "log.h"

#include <windows.h>
#include <tlhelp32.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

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
        code != EXCEPTION_FLT_STACK_CHECK &&  // the float ones only with GT4_FPU_TRAP (kernel.cpp)
        code != EXCEPTION_GUARD_PAGE)  // a touch of another thread's stack guard (the system handles a thread's own)
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

// GT4_WATCH_WRITE=ADDR[,ADDR...][@FILE] (debugging): hardware write watchpoints (up to four guest addresses,
// four bytes each) on every thread, armed when FILE appears (at start without @FILE); each write is logged with
// the native function that made it and its two callers, the first 200 of them. GT4_WATCH_ODD=1: only writes of a
// value no float the game computes would have (a non-zero exponent below 2^-60: an address or an integer read as
// a float).
LONG WINAPI on_watch(EXCEPTION_POINTERS *ep) {
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT *c = ep->ContextRecord;
    if (!(c->Dr6 & 0xF)) return EXCEPTION_CONTINUE_SEARCH;
    static const bool odd_only = getenv("GT4_WATCH_ODD") != nullptr;
    const int slot = __builtin_ctz(static_cast<unsigned>(c->Dr6 & 0xF));
    const DWORD addr = (&c->Dr0)[slot];
    const unsigned value = *reinterpret_cast<const volatile unsigned *>(addr);
    const unsigned exponent = (value >> 23) & 0xFF;
    static volatile LONG logged = 0;
    if ((!odd_only || (exponent != 0 && exponent < 127 - 60)) && InterlockedIncrement(&logged) <= 200) {
        char where[256], caller[256] = "?", caller2[256] = "?";
        describe(c->Eip, where, sizeof where);
        const DWORD *frame = reinterpret_cast<const DWORD *>(c->Ebp);
        if (!IsBadReadPtr(frame, 8)) {
            describe(frame[1], caller, sizeof caller);
            const DWORD *up = reinterpret_cast<const DWORD *>(frame[0]);
            if (!IsBadReadPtr(up, 8)) describe(up[1], caller2, sizeof caller2);
        }
        gt4::rt::log_info("watch: write of %08lx = %08x at %s, called from %s, from %s", static_cast<unsigned long>(addr),
                          value, where, caller, caller2);
    }
    c->Dr6 = 0;
    return EXCEPTION_CONTINUE_EXECUTION;
}

DWORD WINAPI watch_thread(void *) {
    const char *spec = getenv("GT4_WATCH_WRITE");
    DWORD addr[4] = {};
    int n = 0;
    const char *p = spec;
    while (*p && *p != '@' && n < 4) {
        char *end;
        addr[n++] = strtoul(p, &end, 16);
        p = *end == ',' ? end + 1 : end;
        if (end == p && *p != '@') break;
    }
    if (const char *at = strchr(spec, '@'))
        while (GetFileAttributesA(at + 1) == INVALID_FILE_ATTRIBUTES) Sleep(100);
    DWORD dr7 = 0;
    for (int k = 0; k < n; ++k) dr7 |= (1u << (2 * k)) | (0xDu << (16 + 4 * k));   // local enable, write, 4 bytes
    // Threads started later are armed by the next scan (every 100 ms).
    const DWORD self = GetCurrentThreadId();
    static DWORD done[512];
    int armed = 0;
    for (;;) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        THREADENTRY32 te{sizeof te};
        for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
            if (te.th32OwnerProcessID != GetCurrentProcessId() || te.th32ThreadID == self) continue;
            bool known = false;
            for (int k = 0; k < armed && !known; ++k) known = done[k] == te.th32ThreadID;
            if (known || armed >= 512) continue;
            HANDLE h = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
            if (!h) continue;
            SuspendThread(h);
            CONTEXT c{};
            c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (GetThreadContext(h, &c)) {
                for (int k = 0; k < n; ++k) (&c.Dr0)[k] = addr[k];
                c.Dr7 = dr7;
                if (SetThreadContext(h, &c)) done[armed++] = te.th32ThreadID;
            }
            ResumeThread(h);
            CloseHandle(h);
        }
        CloseHandle(snap);
        static int reported = 0;
        if (armed != reported) gt4::rt::log_info("watch: %d write watchpoints armed on %d threads", n, reported = armed);
        Sleep(100);
    }
}

struct Install {
    Install() {
        AddVectoredExceptionHandler(1, on_crash);
        if (const char *w = getenv("GT4_WATCH_WRITE"); w && *w) {
            AddVectoredExceptionHandler(1, on_watch);
            CreateThread(nullptr, 1u << 16, watch_thread, nullptr, 0, nullptr);
        }
        fprintf(stderr, "gt4_game: starting\n");
    }
} g_install __attribute__((init_priority(101)));

}  // namespace
