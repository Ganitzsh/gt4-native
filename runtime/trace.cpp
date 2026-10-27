// Call tracing for game sources compiled with GT4_CC_EXTRA=-finstrument-functions (a debugging aid):
// with GT4_TRACE_FUNCS set, every entry and exit of an instrumented function is logged with its PS2
// address, indented by call depth per thread (at most GT4_TRACE_FUNCS lines, default 20000).
#include "image.h"
#include "log.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {
thread_local int t_depth = 0;
long g_left = -1;

bool tracing() {
    if (g_left < 0) {
        const char *e = getenv("GT4_TRACE_FUNCS");
        g_left = e ? (atol(e) > 1 ? atol(e) : 20000) : 0;
    }
    return g_left > 0;
}
}  // namespace

extern "C" void gt4_trace_dump_entry(uint32_t fn, const uint32_t *args);

// GT4_TRACE_HITS=PATH: the PS2 address of each instrumented function the first time it runs, appended to
// PATH as it happens (the game is usually terminated, not exited): which functions a scene uses.
namespace {
std::atomic<uint32_t> g_hits[16384];
FILE *g_hit_file = nullptr;
int g_hits_on = -1;

__attribute__((no_instrument_function)) void record_hit(uint32_t fn) {
    if (g_hits_on < 0) {
        const char *e = getenv("GT4_TRACE_HITS");
        g_hit_file = e ? fopen(e, "a") : nullptr;
        g_hits_on = g_hit_file != nullptr;
    }
    if (!g_hits_on) return;
    uint32_t slot = (fn * 2654435761u) >> 18;   // 14 bits
    for (int probes = 0; probes < 16384; ++probes, slot = (slot + 1) & 16383) {
        uint32_t seen = g_hits[slot].load(std::memory_order_relaxed);
        if (seen == fn) return;
        if (seen == 0 && g_hits[slot].compare_exchange_strong(seen, fn)) {
            uint32_t guest = 0;
            const char *name = gt4::rt::native_name_for(fn, &guest);
            fprintf(g_hit_file, "%08x %s\n", guest, name ? name : "?");
            fflush(g_hit_file);
            return;
        }
        if (seen == fn) return;
    }
}
}  // namespace

extern "C" __attribute__((no_instrument_function, optimize("no-omit-frame-pointer"))) void
__cyg_profile_func_enter(void *fn, void *site) {
    {
        const auto *fr = static_cast<const uint32_t *>(__builtin_frame_address(1));
        gt4_trace_dump_entry(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(fn)), fr + 2);
        record_hit(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(fn)));
    }
    if (!tracing()) return;
    --g_left;
    uint32_t g = 0, caller = 0;
    gt4::rt::native_name_for(reinterpret_cast<uintptr_t>(fn), &g);
    gt4::rt::native_name_for(reinterpret_cast<uintptr_t>(site), &caller);
    // the traced function's first arguments: its frame (gcc 2.9 keeps frame pointers) + 8
    const auto *frame = static_cast<const uint32_t *>(__builtin_frame_address(1));
    const uint32_t *args = frame + 2;
    // arguments that point at text are shown as strings, a%d="..."; arguments that point at a
    // pointer to text (the game's String objects) as a%d=&"..."
    char strs[200] = "";
    int n = 0;
    auto readable = [](uint32_t a, MEMORY_BASIC_INFORMATION &mi) {
        return a >= 0x10000 && VirtualQuery(reinterpret_cast<const void *>(static_cast<uintptr_t>(a)), &mi, sizeof mi) &&
               mi.State == MEM_COMMIT && !(mi.Protect & (PAGE_NOACCESS | PAGE_GUARD));
    };
    auto text_len = [](const unsigned char *p, const MEMORY_BASIC_INFORMATION &mi) {
        int len = 0;
        while (len < 24 && p + len < static_cast<const unsigned char *>(mi.BaseAddress) + mi.RegionSize &&
               p[len] >= 0x20 && p[len] < 0x7F)
            ++len;
        return len;
    };
    for (int i = 0; i < 5 && n < 150; ++i) {
        MEMORY_BASIC_INFORMATION mi;
        if (!readable(args[i], mi)) continue;
        const auto *p = reinterpret_cast<const unsigned char *>(static_cast<uintptr_t>(args[i]));
        int len = text_len(p, mi);
        if (len >= 3) {
            n += snprintf(strs + n, sizeof strs - n, " a%d=\"%.*s\"", i, len, p);
            continue;
        }
        if ((args[i] & 3) == 0 &&
            p + 4 <= static_cast<const unsigned char *>(mi.BaseAddress) + mi.RegionSize) {
            uint32_t inner = *reinterpret_cast<const uint32_t *>(p);
            MEMORY_BASIC_INFORMATION mi2;
            if (!readable(inner, mi2)) continue;
            const auto *q = reinterpret_cast<const unsigned char *>(static_cast<uintptr_t>(inner));
            len = text_len(q, mi2);
            if (len >= 2) n += snprintf(strs + n, sizeof strs - n, " a%d=&\"%.*s\"", i, len, q);
        }
    }
    gt4::rt::log_info("%*s> %08x/%08x (from %08x) args %08x %08x %08x %08x %08x%s", 2 * (t_depth < 40 ? t_depth : 40),
                      "", g, static_cast<unsigned>(reinterpret_cast<uintptr_t>(fn)), caller, args[0], args[1], args[2],
                      args[3], args[4], strs);
    ++t_depth;
}

// The exit hook runs in the traced function's epilogue with its return value still in %eax: a naked
// entry captures it before any compiled code can change it, and keeps %edx (64-bit and 8-byte
// struct results) and %ecx.
extern "C" void gt4_trace_exit(void *fn, uint32_t ret);
__asm__(".text\n.globl ___cyg_profile_func_exit\n___cyg_profile_func_exit:\n"
        "  pushl %edx\n  pushl %ecx\n  pushl %eax\n  pushl 16(%esp)\n  call _gt4_trace_exit\n"
        "  addl $4, %esp\n  popl %eax\n  popl %ecx\n  popl %edx\n  ret\n");

// GT4_TRACE_DUMP=ADDR:ARG:LEN[:OFF]: dump LEN bytes at argument number ARG of the function at PS2
// address ADDR, at its entry and when it returns. With OFF, the argument is a pointer to a pointer:
// the dump is at *ARG + OFF (OFF may be negative, e.g. -10 for a string's header).
namespace {
uint32_t g_dump_fn = 0, g_dump_arg = 0, g_dump_len = 0;
int g_dump_off = 0;
bool g_dump_deref = false;
thread_local uint32_t t_dump_ptr = 0;
bool dump_setup() {
    static bool done = false;
    if (!done) {
        done = true;
        if (const char *e = getenv("GT4_TRACE_DUMP")) {
            char off[16] = "";
            if (sscanf(e, "%x:%u:%x:%15s", &g_dump_fn, &g_dump_arg, &g_dump_len, off) == 4) {
                g_dump_deref = true;
                g_dump_off = off[0] == '-' ? -static_cast<int>(strtoul(off + 1, nullptr, 16))
                                           : static_cast<int>(strtoul(off, nullptr, 16));
            }
        }
    }
    return g_dump_fn != 0;
}

__attribute__((no_instrument_function)) void dump(const char *when, uint32_t ptr) {
    MEMORY_BASIC_INFORMATION mi;
    if (g_dump_deref) {
        if (!VirtualQuery(reinterpret_cast<void *>(static_cast<uintptr_t>(ptr)), &mi, sizeof mi) ||
            mi.State != MEM_COMMIT) {
            gt4::rt::log_info("dump %s: %08x unreadable", when, ptr);
            return;
        }
        uint32_t inner = *reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(ptr));
        gt4::rt::log_info("dump %s: *%08x = %08x", when, ptr, inner);
        ptr = inner + g_dump_off;
    }
    if (!VirtualQuery(reinterpret_cast<void *>(static_cast<uintptr_t>(ptr)), &mi, sizeof mi) || mi.State != MEM_COMMIT) {
        gt4::rt::log_info("dump %s: %08x unreadable", when, ptr);
        return;
    }
    const auto *p = reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(ptr));
    for (uint32_t off = 0; off < g_dump_len; off += 16)
        gt4::rt::log_info("dump %s %08x+%03x: %08x %08x %08x %08x", when, ptr, off, p[off / 4], p[off / 4 + 1],
                          p[off / 4 + 2], p[off / 4 + 3]);
}
}  // namespace

extern "C" __attribute__((no_instrument_function)) void gt4_trace_dump_entry(uint32_t fn, const uint32_t *args) {
    if (dump_setup() && fn == g_dump_fn) {
        t_dump_ptr = args[g_dump_arg];
        dump("entry", t_dump_ptr);
    }
}

extern "C" __attribute__((no_instrument_function)) void gt4_trace_exit(void *fn, uint32_t ret) {
    if (dump_setup() && static_cast<uint32_t>(reinterpret_cast<uintptr_t>(fn)) == g_dump_fn && t_dump_ptr) {
        dump("exit", t_dump_ptr);
        t_dump_ptr = 0;
    }
    if (!tracing()) return;
    --t_depth;
    gt4::rt::log_info("%*s< %08x = %08x", 2 * (t_depth < 40 ? t_depth : 40), "",
                      static_cast<unsigned>(reinterpret_cast<uintptr_t>(fn)), ret);
}
