// GT4_RECORD=ADDR[,ADDR...][@VBLANK][xCOUNT]: records calls of game functions for the differential replay
// (tools/eeref): the first COUNT calls (default 1) of each PS2 function ADDR made at or after VBlank
// VBLANK (default 0). Calls may nest (a function and its callees). For each, two files go to
// GT4_RECORD_DIR (default "rec"). With GT4_RECORD_ROOT=ADDR, the functions are recorded only inside the
// first recorded call of ADDR (itself recorded too): a step and the calls it makes, from one state.
//   ADDR_N.pre   the arguments (the caller's 16 stack words), VU0's registers and memory, the
//                scratchpad, the 32 MB of guest RAM and the caller's 64 KB of native stack (arguments
//                often point at its locals), at the function's entry;
//   ADDR_N.post  the same at its return (of RAM only the 4 KB pages that changed), with eax and edx
//                (game code returns floats in eax).
// The replay runs the original MIPS code of ADDR on the .pre state and compares with the .post state.
//
// How: an int3 on the first byte of the native function (gcc 2.9's `push %ebp`, which the handler then
// performs itself), and the return address swapped for runtime/record_stub.s. Interrupts are held from
// entry to return (KernelScope), so their handlers write nothing in between.
#include "guest.h"
#include "image.h"
#include "kernel.h"
#include "log.h"
#include "native/vu0.h"
#include "runtime.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <new>
#include <vector>

extern "C" void gt4_record_return();  // record_stub.s
extern "C" int gt4_sys_GetThreadId();

namespace {

using namespace gt4::rt;

struct RecHeader {
    char magic[8];  // "GT4REC3"
    uint32_t addr, index, vblank, kind;  // kind 0 = entry, 1 = return
    uint32_t args[16];
    uint32_t eax, edx;
    uint32_t ram_size, spr_size, vu_size, vu0_state_size;
    uint32_t stack_base, stack_size;  // a window of the native stack, from the arguments up
    uint32_t ram_pages;  // .post: only the 4 KB pages of RAM that changed, each as its address and contents
};

constexpr uint32_t kVuMem = 0x11000000, kVuMemSize = 0x5000;  // VU0 micro (4 KB) and data (4 KB at +0x4000)
constexpr uint32_t kPage = 0x1000;

struct Hook {
    uint32_t addr;
    uint8_t *fn;
    volatile LONG taken;  // calls recorded so far
};
constexpr int kMaxHooks = 1024;
Hook g_hooks[kMaxHooks];
int g_hook_count;
bool g_rooted;  // g_hooks[0] is GT4_RECORD_ROOT
uint32_t g_vblank, g_count = 1;
const char *g_dir = "rec";

struct Pending {
    const Hook *hook;
    uint32_t ret, index, stack;
    uint8_t *ram;  // guest RAM at entry, to write only the pages that changed at return
    alignas(KernelScope) unsigned char scope[sizeof(KernelScope)];
};
thread_local Pending t_pending[8];
thread_local int t_depth;

// The window of the native stack recorded with a call: from its arguments up 64 KB, or to the stack's top.
uint32_t stack_window(uint32_t from) {
    MEMORY_BASIC_INFORMATION mi;
    if (!VirtualQuery(reinterpret_cast<void *>(static_cast<uintptr_t>(from)), &mi, sizeof mi)) return 0;
    const uint32_t top = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(mi.BaseAddress) + mi.RegionSize);
    return top - from < 0x10000 ? top - from : 0x10000;
}

// kind 0: the whole RAM, and a copy of it into entry_ram; kind 1: the pages that differ from entry_ram.
void write_state(const Hook &hook, uint32_t index, uint32_t kind, const uint32_t *args, uint32_t stack, uint32_t eax,
                 uint32_t edx, uint8_t *entry_ram) {
    char path[512];
    snprintf(path, sizeof path, "%s/%08x_%u.%s", g_dir, hook.addr, index, kind ? "post" : "pre");
    FILE *f = fopen(path, "wb");
    if (!f) {
        log_error("record: cannot write %s", path);
        return;
    }
    RecHeader h{};
    std::memcpy(h.magic, "GT4REC3", 8);
    h.addr = hook.addr;
    h.index = index;
    h.vblank = kernel_vblank_count();
    h.kind = kind;
    if (args) std::memcpy(h.args, args, sizeof h.args);
    h.eax = eax;
    h.edx = kind ? edx : static_cast<uint32_t>(gt4_sys_GetThreadId());   // an entry: the guest thread (eeref's GetThreadId)
    h.ram_size = kRamSize;
    h.spr_size = kScratchpadSize;
    h.vu_size = kVuMemSize;
    h.vu0_state_size = sizeof(Gt4Vu0State);
    h.stack_base = stack;
    h.stack_size = stack_window(stack);
    const auto *ram = static_cast<const uint8_t *>(guest_ptr(0));
    // GT4_RECORD_DELTA (many functions recorded at once): the first entry writes the whole RAM to base.ram; every
    // entry is then kind 2, the pages that differ from the entry recorded before it (in order.txt), and the base
    // follows; replay.py rebuilds the full entries by walking the chain.
    static const bool delta = getenv("GT4_RECORD_DELTA") != nullptr;
    static uint8_t *base = nullptr;
    if (!kind && delta) {
        if (!base) {
            base = static_cast<uint8_t *>(VirtualAlloc(nullptr, kRamSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
            std::memcpy(base + kGuestSectionStart, ram + kGuestSectionStart, kRamSize - kGuestSectionStart);
            char bpath[512];
            snprintf(bpath, sizeof bpath, "%s/base.ram", g_dir);
            if (FILE *b = fopen(bpath, "wb")) {
                fwrite(base, 1, kRamSize, b);
                fclose(b);
            }
        }
        h.kind = 2;
    }
    // The pages that differ, copied in one pass: the game's other threads go on writing RAM meanwhile, and a
    // second pass could find another set than the count in the header (every later section then shifts).
    std::vector<uint8_t> pages;
    if (h.kind == 2 || kind) {
        const uint8_t *ref = kind ? entry_ram : base;
        for (uint32_t a = kGuestSectionStart; a < kRamSize; a += kPage)
            if (std::memcmp(ram + a, ref + a, kPage) != 0) {
                const size_t at = pages.size();
                pages.resize(at + 4 + kPage);
                std::memcpy(&pages[at], &a, 4);
                std::memcpy(&pages[at + 4], ram + a, kPage);
            }
        h.ram_pages = static_cast<uint32_t>(pages.size() / (4 + kPage));
    }
    fwrite(&h, sizeof h, 1, f);
    fwrite(&__gt4_vu0, sizeof(Gt4Vu0State), 1, f);
    if (h.kind == 2) {
        fwrite(pages.data(), 1, pages.size(), f);
        for (size_t at = 0; at < pages.size(); at += 4 + kPage) {   // the next entry's reference
            uint32_t a;
            std::memcpy(&a, &pages[at], 4);
            std::memcpy(base + a, &pages[at + 4], kPage);
        }
        std::memcpy(entry_ram + kGuestSectionStart, ram + kGuestSectionStart, kRamSize - kGuestSectionStart);
        char opath[512];
        snprintf(opath, sizeof opath, "%s/order.txt", g_dir);
        if (FILE *o = fopen(opath, "a")) {
            fprintf(o, "%08x_%u.pre\n", hook.addr, index);
            fclose(o);
        }
    } else if (!kind) {
        static const uint8_t zeros[kGuestSectionStart] = {};
        fwrite(zeros, 1, kGuestSectionStart, f);  // the unmapped first 68 KB
        fwrite(ram + kGuestSectionStart, 1, kRamSize - kGuestSectionStart, f);
        std::memcpy(entry_ram + kGuestSectionStart, ram + kGuestSectionStart, kRamSize - kGuestSectionStart);
    } else {
        fwrite(pages.data(), 1, pages.size(), f);
    }
    fwrite(guest_ptr(kScratchpad), 1, kScratchpadSize, f);
    fwrite(guest_ptr(kVuMem), 1, kVuMemSize, f);
    fwrite(guest_ptr(h.stack_base), 1, h.stack_size, f);
    fclose(f);
}

// Puts the int3 on a hooked function's first byte (push %ebp), or takes it off.
void arm(const Hook &hook, bool on) {
    if (!hook.fn) return;
    DWORD old;
    VirtualProtect(hook.fn, 1, PAGE_EXECUTE_READWRITE, &old);
    *hook.fn = on ? 0xCC : 0x55;
    VirtualProtect(hook.fn, 1, old, &old);
    FlushInstructionCache(GetCurrentProcess(), hook.fn, 1);
}

// With a root, the other hooks are armed only during its recorded call: a breakpoint costs microseconds,
// and accessors hit every frame would slow the menus off the pad script's timing.
void arm_scoped(bool on) {
    for (int k = 1; k < g_hook_count; ++k) arm(g_hooks[k], on);
}

LONG CALLBACK on_break(EXCEPTION_POINTERS *e) {
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_BREAKPOINT) return EXCEPTION_CONTINUE_SEARCH;
    Hook *hook = nullptr;
    for (int k = 0; k < g_hook_count && !hook; ++k)
        if (e->ExceptionRecord->ExceptionAddress == g_hooks[k].fn) hook = &g_hooks[k];
    if (!hook) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT *c = e->ContextRecord;
    auto *sp = reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(c->Esp));
    const bool in_scope = !g_rooted || (hook == &g_hooks[0] ? hook->taken == 0 : t_depth > 0 && t_pending[0].hook == &g_hooks[0]);
    if (in_scope && kernel_vblank_count() >= g_vblank && hook->taken < static_cast<LONG>(g_count) && t_depth < 8) {
        Pending &p = t_pending[t_depth++];
        p.hook = hook;
        p.index = static_cast<uint32_t>(InterlockedIncrement(&hook->taken) - 1);
        new (p.scope) KernelScope;  // no interrupt handler between entry and return
        p.stack = c->Esp + 4;  // the arguments, then the caller's frame
        if (!p.ram) p.ram = static_cast<uint8_t *>(VirtualAlloc(nullptr, kRamSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        write_state(*hook, p.index, 0, sp + 1, p.stack, 0, 0, p.ram);
        p.ret = sp[0];
        sp[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gt4_record_return));
        log_info("record: call %u of %08x at VBlank %u", p.index, hook->addr, kernel_vblank_count());
        if (g_rooted && hook == &g_hooks[0]) arm_scoped(true);
    }
    // a hook that has its calls no longer traps (hundreds of hooks at once: GT4_RECORD over every draft)
    if (!g_rooted && hook->taken >= static_cast<LONG>(g_count)) arm(*hook, false);
    // the instruction the int3 replaced: push %ebp
    c->Esp -= 4;
    *reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(c->Esp)) = c->Ebp;
    c->Eip = static_cast<DWORD>(reinterpret_cast<uintptr_t>(hook->fn) + 1);
    return EXCEPTION_CONTINUE_EXECUTION;
}

// GT4_SNAPSHOT=VBLANK[,VBLANK...]: the guest RAM as it is when each VBlank is reached, as GT4_RECORD_DIR/
// snap_VBLANK.ram (32 MB, from address 0), to read game state between two runs (a thread polls; the game goes on
// meanwhile, so a structure being written can be half old, half new).
DWORD WINAPI snapshot_thread(void *) {
    std::vector<uint32_t> at;
    for (const char *p = getenv("GT4_SNAPSHOT"); *p;) {
        char *end;
        at.push_back(static_cast<uint32_t>(strtoul(p, &end, 0)));
        p = *end == ',' ? end + 1 : end;
        if (end == p && *p) break;
    }
    static const uint8_t zeros[kGuestSectionStart] = {};
    // trigger:FILE: whenever FILE appears (it is deleted), the RAM and the scratchpad (snap_N.spr, 16 KB at
    // 0x70000000), named by the VBlank then; for screens reached by hand.
    if (std::strncmp(getenv("GT4_SNAPSHOT"), "trigger:", 8) == 0) {
        const char *file = getenv("GT4_SNAPSHOT") + 8;
        for (;;) {
            Sleep(200);
            if (GetFileAttributesA(file) == INVALID_FILE_ATTRIBUTES) continue;
            DeleteFileA(file);
            const uint32_t v = kernel_vblank_count();
            char path[512];
            snprintf(path, sizeof path, "%s/snap_%u.ram", g_dir, v);
            if (FILE *f = fopen(path, "wb")) {
                fwrite(zeros, 1, kGuestSectionStart, f);
                fwrite(guest_ptr(kGuestSectionStart), 1, kRamSize - kGuestSectionStart, f);
                fclose(f);
            }
            snprintf(path, sizeof path, "%s/snap_%u.spr", g_dir, v);
            if (FILE *f = fopen(path, "wb")) {
                fwrite(guest_ptr(0x70000000), 1, 0x4000, f);
                fclose(f);
            }
            log_info("record: guest RAM and scratchpad at VBlank %u -> %s/snap_%u.*", v, g_dir, v);
        }
    }
    for (uint32_t v : at) {
        while (kernel_vblank_count() < v) Sleep(1);
        char path[512];
        snprintf(path, sizeof path, "%s/snap_%u.ram", g_dir, v);
        if (FILE *f = fopen(path, "wb")) {
            fwrite(zeros, 1, kGuestSectionStart, f);
            fwrite(guest_ptr(kGuestSectionStart), 1, kRamSize - kGuestSectionStart, f);
            fclose(f);
            log_info("record: guest RAM at VBlank %u -> %s", kernel_vblank_count(), path);
        }
    }
    return 0;
}

// The address of a GT4_POKE target: hex ADDR, or *WORD+OFF / *WORD-OFF (hex): OFF from the first guest word, in
// RAM from 0x00100000, that holds WORD (an object found by its vtable pointer, wherever the heap put it).
uint32_t poke_address(const char *p, char **end) {
    if (*p != '*') return static_cast<uint32_t>(strtoul(p, end, 16));
    const uint32_t word = static_cast<uint32_t>(strtoul(p + 1, end, 16));
    const int sign = **end == '-' ? -1 : 1;
    const uint32_t off = (**end == '+' || **end == '-') ? static_cast<uint32_t>(strtoul(*end + 1, end, 16)) : 0;
    for (uint32_t a = 0x00100000; a < kRamSize; a += 4)
        if (*static_cast<volatile uint32_t *>(guest_ptr(a)) == word) return a + sign * off;
    log_warn("record: GT4_POKE: no guest word %08X", word);
    return 0;
}

// GT4_POKE=VBLANK:ADDR=WORD[,ADDR=WORD...][;VBLANK:...] (hex addresses and words; ADDR as poke_address): writes
// those guest words when each VBlank is reached, to put game state into a case under test (e.g. an event's
// parameters).
DWORD WINAPI poke_thread(void *) {
    const char *p = getenv("GT4_POKE");
    while (*p) {
        char *end;
        const uint32_t v = static_cast<uint32_t>(strtoul(p, &end, 0));
        if (*end != ':') break;
        p = end + 1;
        while (kernel_vblank_count() < v) Sleep(1);
        while (*p && *p != ';') {
            const uint32_t addr = poke_address(p, &end);
            if (*end != '=') return 0;
            const uint32_t word = static_cast<uint32_t>(strtoul(end + 1, &end, 16));
            if (addr) *static_cast<volatile uint32_t *>(guest_ptr(addr)) = word;
            log_info("record: VBlank %u: [%08X] = %08X", kernel_vblank_count(), addr, word);
            p = *end == ',' ? end + 1 : end;
        }
        if (*p == ';') ++p;
    }
    return 0;
}

}  // namespace

void gt4::rt::record_install() {
    if (const char *d = getenv("GT4_RECORD_DIR")) g_dir = d;
    if (const char *s = getenv("GT4_POKE"); s && *s) CreateThread(nullptr, 1u << 16, poke_thread, nullptr, 0, nullptr);
    if (const char *s = getenv("GT4_SNAPSHOT"); s && *s) {
        CreateDirectoryA(g_dir, nullptr);
        CreateThread(nullptr, 1u << 16, snapshot_thread, nullptr, 0, nullptr);
    }
    const char *spec = getenv("GT4_RECORD");
    if (!spec || !*spec) return;
    if (const char *root = getenv("GT4_RECORD_ROOT")) {
        g_hooks[g_hook_count++].addr = static_cast<uint32_t>(strtoul(root, nullptr, 16));
        g_rooted = true;
    }
    char *end = const_cast<char *>(spec);
    do {
        const uint32_t addr = static_cast<uint32_t>(strtoul(end + (*end == ','), &end, 16));
        if (g_hook_count < kMaxHooks) g_hooks[g_hook_count++].addr = addr;
    } while (*end == ',');
    if (*end == '@') g_vblank = static_cast<uint32_t>(strtoul(end + 1, &end, 0));
    if (*end == 'x') g_count = static_cast<uint32_t>(strtoul(end + 1, &end, 0));
    if (const char *d = getenv("GT4_RECORD_DIR")) g_dir = d;
    CreateDirectoryA(g_dir, nullptr);
    for (int h = 0; h < g_hook_count; ++h) {
        Hook &hook = g_hooks[h];
        for (uint32_t k = 0; k < gt4_native_function_count; ++k)
            if (gt4_native_functions[k].addr == hook.addr) hook.fn = static_cast<uint8_t *>(gt4_native_functions[k].fn);
        if (!hook.fn || *hook.fn != 0x55) {
            log_error("record: %08x has no native function starting with push %%ebp", hook.addr);
            hook.fn = nullptr;
            continue;
        }
        if (!g_rooted || h == 0) arm(hook, true);
    }
    AddVectoredExceptionHandler(1, on_break);
    log_info("record: %d function(s) from VBlank %u, %u call(s) each, into %s", g_hook_count, g_vblank, g_count, g_dir);
}

// From record_stub.s when a recorded call returns: regs is its pushal frame (eax at [7], edx at [5]).
// Returns the caller's return address.
extern "C" uint32_t gt4_record_after(const uint32_t *regs) {
    Pending &p = t_pending[--t_depth];
    write_state(*p.hook, p.index, 1, nullptr, p.stack, regs[7], regs[5], p.ram);
    if (g_rooted && p.hook == &g_hooks[0]) {
        arm_scoped(false);
        arm(g_hooks[0], false);  // recorded once
    }
    reinterpret_cast<KernelScope *>(p.scope)->~KernelScope();
    return p.ret;
}
