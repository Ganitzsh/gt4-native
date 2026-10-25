// Call tracing for game sources compiled with GT4_CC_EXTRA=-finstrument-functions (a debugging aid):
// with GT4_TRACE_FUNCS set, every entry and exit of an instrumented function is logged with its PS2
// address, indented by call depth per thread (at most GT4_TRACE_FUNCS lines, default 20000).
#include "image.h"
#include "log.h"

#include <windows.h>

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

extern "C" __attribute__((no_instrument_function, optimize("no-omit-frame-pointer"))) void
__cyg_profile_func_enter(void *fn, void *site) {
    {
        const auto *fr = static_cast<const uint32_t *>(__builtin_frame_address(1));
        gt4_trace_dump_entry(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(fn)), fr + 2);
    }
    if (!tracing()) return;
    --g_left;
    uint32_t g = 0, caller = 0;
    gt4::rt::native_name_for(reinterpret_cast<uintptr_t>(fn), &g);
    gt4::rt::native_name_for(reinterpret_cast<uintptr_t>(site), &caller);
    // the traced function's first arguments: its frame (gcc 2.9 keeps frame pointers) + 8
    const auto *frame = static_cast<const uint32_t *>(__builtin_frame_address(1));
    const uint32_t *args = frame + 2;
    // arguments that point at text are shown as strings
    char strs[160] = "";
    int n = 0;
    for (int i = 0; i < 5 && n < 120; ++i) {
        MEMORY_BASIC_INFORMATION mi;
        const auto *p = reinterpret_cast<const unsigned char *>(static_cast<uintptr_t>(args[i]));
        if (args[i] < 0x10000 || !VirtualQuery(p, &mi, sizeof mi) || mi.State != MEM_COMMIT ||
            (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
            continue;
        int len = 0;
        while (len < 24 && p + len < static_cast<const unsigned char *>(mi.BaseAddress) + mi.RegionSize &&
               p[len] >= 0x20 && p[len] < 0x7F)
            ++len;
        if (len >= 3) n += snprintf(strs + n, sizeof strs - n, " a%d=\"%.*s\"", i, len, p);
    }
    gt4::rt::log_info("%*s> %08x/%08x (from %08x) args %08x %08x %08x %08x %08x%s", 2 * (t_depth < 40 ? t_depth : 40),
                      "", g, static_cast<unsigned>(reinterpret_cast<uintptr_t>(fn)), caller, args[0], args[1], args[2],
                      args[3], args[4], strs);
    ++t_depth;
}

// The exit hook runs in the traced function's epilogue with its return value still in %eax: a naked
// entry captures it before any compiled code can change it.
extern "C" void gt4_trace_exit(void *fn, uint32_t ret);
__asm__(".text\n.globl ___cyg_profile_func_exit\n___cyg_profile_func_exit:\n"
        "  pushl %eax\n  pushl 8(%esp)\n  call _gt4_trace_exit\n  addl $4, %esp\n  popl %eax\n  ret\n");

// GT4_TRACE_DUMP=ADDR:ARG:LEN: when the function at PS2 address ADDR returns, dump LEN bytes at its
// argument number ARG (as it was at entry).
namespace {
uint32_t g_dump_fn = 0, g_dump_arg = 0, g_dump_len = 0;
thread_local uint32_t t_dump_ptr = 0;
bool dump_setup() {
    static bool done = false;
    if (!done) {
        done = true;
        if (const char *e = getenv("GT4_TRACE_DUMP"))
            sscanf(e, "%x:%u:%x", &g_dump_fn, &g_dump_arg, &g_dump_len);
    }
    return g_dump_fn != 0;
}
}  // namespace

extern "C" __attribute__((no_instrument_function)) void gt4_trace_dump_entry(uint32_t fn, const uint32_t *args) {
    if (dump_setup() && fn == g_dump_fn) t_dump_ptr = args[g_dump_arg];
}

extern "C" __attribute__((no_instrument_function)) void gt4_trace_exit(void *fn, uint32_t ret) {
    if (dump_setup() && static_cast<uint32_t>(reinterpret_cast<uintptr_t>(fn)) == g_dump_fn && t_dump_ptr) {
        const auto *p = reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(t_dump_ptr));
        for (uint32_t off = 0; off < g_dump_len; off += 16)
            gt4::rt::log_info("dump %08x+%03x: %08x %08x %08x %08x", t_dump_ptr, off, p[off / 4], p[off / 4 + 1],
                              p[off / 4 + 2], p[off / 4 + 3]);
        t_dump_ptr = 0;
    }
    if (!tracing()) return;
    --t_depth;
    gt4::rt::log_info("%*s< %08x = %08x", 2 * (t_depth < 40 ? t_depth : 40), "",
                      static_cast<unsigned>(reinterpret_cast<uintptr_t>(fn)), ret);
}
