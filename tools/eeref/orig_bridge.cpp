// The original code inside the native game, for bisecting behaviour (docs/DIFFERENTIAL_TESTING.md): with
// gt4_game linked by `game_link.py --eeorig build/eeref/gen`, GT4_ORIG=ADDR[=KINDS[:RET]][,...] runs the
// translated MIPS code of each PS2 function ADDR (eerecomp, third_party/ps2-web-recomp) instead of its native
// code, on the live guest memory: the whole call tree under it is the original's.
//
// KINDS are the native parameters as tools/eeref takes them (i, w, f, q), RET the return (v, i, w, f); by default
// those of GT4_ORIG_KINDS=FILE (tools/eeref/kinds.py), else eight i and i. How: an int3 on the native function's
// first byte; the handler sends the call to gt4_orig_entry, which runs the original with the arguments from the
// caller's stack and returns its result as the native function would (edx:eax, a float in eax). GT4_ORIG=all
// makes every PS2 function address lead to the original code instead: the native game code then never runs.
// Each thread has an EE stack buffer of its own; VU0's registers are copied in and out; vcallms runs on the
// native VU0 interpreter.
#include "ps2/generated.hpp"

#include "guest.h"
#include "image.h"
#include "log.h"
#include "native/vu0.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>

void register_generated_code();

extern "C" {
void __gt4_ee_ei(void);
void __gt4_ee_di(void);
unsigned int __gt4_ee_mfc0(int reg);
void __gt4_ee_mtc0(int reg, unsigned int value);
int __gt4_ee_syscall(int number, unsigned int a0, unsigned int a1, unsigned int a2, unsigned int a3);
void gt4_game_text_end();  // tools/game_link.py: the end of the game's code, the start of the runtime's
void gt4_replace_setjmp();   // runtime/setjmp.s
void gt4_replace_longjmp();
}

// g_ram and g_spr are placed at 0 and 0x70000000 by the link (--defsym): guest addresses are host addresses.
namespace ps2 {

namespace {

constexpr std::uint32_t kReturn = 0x00001000u;  // $ra of a bridged call
std::vector<UnitInfo> g_units;
std::uint32_t g_origin, g_unit_size = 1;
std::vector<const UnitInfo *> g_index;
thread_local bool t_halted;

template <class T> T rd(std::uint32_t a) {
    T v;
    std::memcpy(&v, reinterpret_cast<const void *>(static_cast<uintptr_t>(a)), sizeof v);
    return v;
}
template <class T> void wr(std::uint32_t a, T v) { std::memcpy(reinterpret_cast<void *>(static_cast<uintptr_t>(a)), &v, sizeof v); }

}  // namespace

// Outside RAM and the scratchpad: the EE stack buffer, VU memory, the uncached mirrors, all mapped at their
// addresses in this process.
std::uint8_t slow_ld8(std::uint32_t a) { return rd<std::uint8_t>(a); }
std::uint16_t slow_ld16(std::uint32_t a) { return rd<std::uint16_t>(a); }
std::uint32_t slow_ld32(std::uint32_t a) { return rd<std::uint32_t>(a); }
std::uint64_t slow_ld64(std::uint32_t a) { return rd<std::uint64_t>(a); }
void slow_ld128(std::uint32_t a, Reg128 &out) { std::memcpy(&out, reinterpret_cast<const void *>(static_cast<uintptr_t>(a)), 16); }
void slow_st8(std::uint32_t a, std::uint8_t v) { wr(a, v); }
void slow_st16(std::uint32_t a, std::uint16_t v) { wr(a, v); }
void slow_st32(std::uint32_t a, std::uint32_t v) { wr(a, v); }
void slow_st64(std::uint32_t a, std::uint64_t v) { wr(a, v); }
void slow_st128(std::uint32_t a, const Reg128 &v) { std::memcpy(reinterpret_cast<void *>(static_cast<uintptr_t>(a)), &v, 16); }

void register_units(const UnitInfo *units, std::uint32_t count) {
    g_units.assign(units, units + count);
    std::sort(g_units.begin(), g_units.end(), [](const UnitInfo &a, const UnitInfo &b) { return a.base < b.base; });
    g_origin = g_units.front().base;
    g_unit_size = g_units.front().size;
    g_index.assign((g_units.back().base - g_origin) / g_unit_size + 1, nullptr);
    for (const UnitInfo &u : g_units) g_index[(u.base - g_origin) / g_unit_size] = &u;
}
void register_modules(const ModuleInfo *, std::uint32_t) {}

void stop(Cpu &c, std::uint32_t pc, const char *what) {
    gt4::rt::log_error("orig: stopped at %08X (ra %08X): %s", pc, c.r[31].w[0], what);
    t_halted = true;
    c.pc = pc | kUnwind;
}

void dispatch(Cpu &c, std::uint32_t, std::uint32_t depth) {
    const std::uint32_t pc = c.pc;
    if ((pc & kUnwind) || pc == kReturn || t_halted) return;
    const std::uint32_t slot = (pc - g_origin) / g_unit_size;
    const UnitInfo *u = pc >= g_origin && slot < g_index.size() ? g_index[slot] : nullptr;
    const std::uint16_t id = u ? u->entries[(pc - u->base) >> 2] : 0;
    if (!id) {
        stop(c, pc, "no translated code");
        return;
    }
    u->function(c, id, depth);
}

void call(Cpu &c, std::uint32_t target, std::uint32_t depth) {
    c.pc = target;
    if (depth >= kMaxDepth) return;
    dispatch(c, 0, depth);
}

void bad_entry(Cpu &c, std::uint32_t) { stop(c, c.pc, "bad entry"); }
bool preempt(Cpu &c) {
    c.budget = 1 << 30;
    return false;
}
void syscall(Cpu &c, std::uint32_t) {
    c.r[2].sd[0] = static_cast<std::int32_t>(__gt4_ee_syscall(static_cast<std::int16_t>(c.r[3].w[0]), c.r[4].w[0], c.r[5].w[0],
                                                              c.r[6].w[0], c.r[7].w[0]));
}
void breakpoint(Cpu &c, std::uint32_t pc) { stop(c, pc, "break"); }
void trap(Cpu &c, std::uint32_t pc) { stop(c, pc, "trap"); }
void probe(Cpu &, std::uint32_t) {}
std::uint32_t mfc0(Cpu &, std::uint32_t reg, std::uint32_t) { return __gt4_ee_mfc0(static_cast<int>(reg)); }
void mtc0(Cpu &, std::uint32_t reg, std::uint32_t value) { __gt4_ee_mtc0(static_cast<int>(reg), value); }
void set_interrupts(Cpu &, bool enabled) {
    if (enabled) __gt4_ee_ei();
    else __gt4_ee_di();
}
std::uint32_t eret(Cpu &c) { return c.cop0[14]; }
bool cpcond0(Cpu &) { return true; }
std::uint32_t cfc1(Cpu &c, std::uint32_t reg) { return reg == 0 ? 0x2E30u : reg == 31 ? c.fcr31 : 0; }
void ctc1(Cpu &c, std::uint32_t reg, std::uint32_t value) {
    if (reg == 31) c.fcr31 = value;
}
std::uint32_t cfc2(Cpu &c, std::uint32_t reg) {
    Vu0Regs &v = c.vu0;
    switch (reg) {
    case 16: return v.status;
    case 17: return v.mac;
    case 18: return v.clip;
    case 20: return v.r;
    case 21: return v.i;
    case 22: return v.q;
    case 26: return v.tpc;
    case 27: return v.cmsar0;
    default: return reg < 16 ? v.vi[reg] : 0;
    }
}
void ctc2(Cpu &c, std::uint32_t reg, std::uint32_t value) {
    Vu0Regs &v = c.vu0;
    switch (reg) {
    case 16: v.status = (v.status & 0x3Fu) | (value & 0xFC0u); break;
    case 17: v.mac = value; break;
    case 18: v.clip = value & 0xFFFFFFu; break;
    case 20: v.r = value & 0x7FFFFFu; break;
    case 21: v.i = value; break;
    case 22: v.q = value; break;
    case 27: v.cmsar0 = value & 0xFFFFu; break;
    default:
        if (reg < 16) v.vi[reg] = static_cast<std::uint16_t>(value);
        break;
    }
}
bool vu0_busy(Cpu &) { return false; }
void vu0_wait(Cpu &) {}
std::uint8_t *vu0_data() { return reinterpret_cast<std::uint8_t *>(static_cast<uintptr_t>(0x11004000u)); }

}  // namespace ps2

namespace {

using namespace ps2;

void vu0_to_native(const Vu0Regs &v) {
    Gt4Vu0State &n = __gt4_vu0;
    for (int k = 0; k < 32; ++k) std::memcpy(n.vf[k], v.vf[k].w, 16);
    std::memcpy(n.acc, v.acc.w, 16);
    for (int k = 0; k < 16; ++k) n.vi[k] = v.vi[k];
    std::memcpy(&n.q, &v.q, 4);
    std::memcpy(&n.i, &v.i, 4);
    std::memcpy(&n.p, &v.p, 4);
    n.r = 0x3F800000u | (v.r & 0x7FFFFFu);
    n.clip = v.clip;
    n.vi[16] = v.status;
    n.vi[17] = v.mac;
}

void vu0_from_native(Vu0Regs &v) {
    const Gt4Vu0State &n = __gt4_vu0;
    for (int k = 0; k < 32; ++k) std::memcpy(v.vf[k].w, n.vf[k], 16);
    std::memcpy(v.acc.w, n.acc, 16);
    for (int k = 0; k < 16; ++k) v.vi[k] = static_cast<std::uint16_t>(n.vi[k]);
    std::memcpy(&v.q, &n.q, 4);
    std::memcpy(&v.i, &n.i, 4);
    std::memcpy(&v.p, &n.p, 4);
    v.r = n.r & 0x7FFFFFu;
    v.clip = n.clip;
    v.status = n.vi[16];
    v.mac = n.vi[17];
}

constexpr std::uint32_t kEeStackSize = 4u << 20;
std::uintptr_t g_thunks, g_thunks_end;  // GT4_ORIG=all's thunks: original code, not native
thread_local Cpu *t_cpu;
thread_local std::uint8_t *t_stack;

}  // namespace

namespace ps2 {
void vcallms(Cpu &c, std::uint32_t address) {
    vu0_to_native(c.vu0);
    __gt4_vu0_callms(address);
    vu0_from_native(c.vu0);
}
}  // namespace ps2

// At every function's first instruction (tools/eeref/hook_entries.py): a function whose trampoline leads into
// the runtime (an SDK replacement, a syscall stub: code that drives the hardware) is called natively, with the
// eight integer argument registers and four stack words as its cdecl arguments; its result goes to $v0 and $f0
// (a float comes back in eax). Game code runs translated, and so do setjmp and longjmp: the native ones would
// save and restore this bridge's x86 frames.
bool gt4_orig_native(Cpu &c, std::uint32_t addr) {
    const auto *p = reinterpret_cast<const std::uint8_t *>(static_cast<uintptr_t>(addr));
    if (p[0] != 0xE9) return false;
    std::int32_t rel;
    std::memcpy(&rel, p + 1, 4);
    const std::uintptr_t target = addr + 5 + static_cast<std::uintptr_t>(rel);
    if (target < reinterpret_cast<std::uintptr_t>(&gt4_game_text_end) ||
        target == reinterpret_cast<std::uintptr_t>(&gt4_replace_setjmp) ||
        target == reinterpret_cast<std::uintptr_t>(&gt4_replace_longjmp) || (target >= g_thunks && target < g_thunks_end))
        return false;
    // the stack arguments: 8-byte slots from $sp (sceSifCallRpc's ninth is its end function's argument)
    using Fn = std::uint64_t (*)(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
                                 std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t);
    const std::uint32_t sp = c.r[29].w[0];
    static const bool trace = getenv("GT4_ORIG_TRACE") != nullptr;  // each native call, with its thread and $sp
    if (trace)
        gt4::rt::log_info("orig: native %08X (%s) from %08X, thread %lu, sp %08X", addr,
                          gt4::rt::native_name_for(target, nullptr), c.r[31].w[0], GetCurrentThreadId(), sp);
    vu0_to_native(c.vu0);
    const std::uint64_t r = reinterpret_cast<Fn>(target)(c.r[4].w[0], c.r[5].w[0], c.r[6].w[0], c.r[7].w[0], c.r[8].w[0],
                                                         c.r[9].w[0], c.r[10].w[0], c.r[11].w[0], ld32(sp), ld32(sp + 8),
                                                         ld32(sp + 16), ld32(sp + 24));
    vu0_from_native(c.vu0);
    c.r[2].sd[0] = static_cast<std::int32_t>(static_cast<std::uint32_t>(r));
    c.f[0] = static_cast<std::uint32_t>(r);
    c.pc = c.r[31].w[0];
    return true;
}


namespace {

// A function's parameters and return as tools/eeref takes them (tools/eeref/kinds.py, GT4_ORIG_KINDS).
struct Sig {
    char kinds[24] = "iiiiiiii";
    char ret = 'i';
};
std::unordered_map<std::uint32_t, Sig> g_sigs;

const Sig &sig_of(std::uint32_t addr) {
    static const Sig fallback;
    const auto it = g_sigs.find(addr);
    return it == g_sigs.end() ? fallback : it->second;
}

// Runs the original code of addr with a native caller's arguments; returns what the native function would
// (edx:eax; a float in eax).
std::uint64_t run_original(const std::uint32_t *args, std::uint32_t addr, const Sig &sig) {
    if (!t_cpu) {
        t_cpu = new Cpu{};
        t_stack = static_cast<std::uint8_t *>(VirtualAlloc(nullptr, kEeStackSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    }
    Cpu &c = *t_cpu;
    // A call that arrives while this thread runs the original already (a native callback, an interrupt
    // handler) gets the EE stack below the running one's, and the running state back afterwards.
    const bool nested = c.pc != 0 && c.pc != kReturn;
    Cpu *saved = nested ? new Cpu(c) : nullptr;
    vu0_from_native(c.vu0);
    std::uint32_t word = 0, ireg = 4, freg = 12;
    for (const char *k = sig.kinds; *k; ++k) {
        if (*k == 'f') {
            if (freg <= 19) c.f[freg++] = args[word];
            word += 1;
        } else if (*k == 'w') {
            if (ireg <= 11) c.r[ireg++].d[0] = args[word] | static_cast<std::uint64_t>(args[word + 1]) << 32;
            word += 2;
        } else if (*k == 'q') {
            if (ireg <= 11) std::memcpy(c.r[ireg++].w, &args[word], 16);
            word += 4;
        } else if (*k == 'i') {
            if (ireg <= 11) c.r[ireg++].sd[0] = static_cast<std::int32_t>(args[word]);
            word += 1;
        }
    }
    const std::uint32_t top = static_cast<std::uint32_t>(reinterpret_cast<uintptr_t>(t_stack)) + kEeStackSize - 0x100;
    c.r[28].sd[0] = 0x006DDDF0;  // $gp (crt0)
    c.r[29].sd[0] = static_cast<std::int32_t>(nested ? (saved->r[29].w[0] - 0x400) & ~15u : top);
    c.r[31].sd[0] = kReturn;
    c.budget = 1 << 30;
    c.pc = addr;
    t_halted = false;
    while (!t_halted && c.pc != kReturn) {  // calls deeper than kMaxDepth unwind to here and go on
        c.pc &= ~kUnwind;
        dispatch(c, 0, 0);
    }
    if (t_halted) gt4::rt::fatal("orig: %08X did not return (GT4_ORIG)", addr);
    vu0_to_native(c.vu0);
    std::uint64_t r = 0;
    if (sig.ret == 'f') r = c.f[0];
    else if (sig.ret == 'w') r = c.r[2].d[0];
    else if (sig.ret == 'i') r = c.r[2].w[0];
    if (saved) {
        const Vu0Regs vu = c.vu0;
        c = *saved;
        c.vu0 = vu;
        delete saved;
    } else {
        c.pc = kReturn;
    }
    return r;
}

struct Hook {
    std::uint32_t addr;
    std::uint8_t *fn;
    Sig sig;
};
constexpr int kMaxHooks = 64;
Hook g_hooks[kMaxHooks];
int g_hook_count;

}  // namespace

// From the entry stubs below: args points at the native caller's arguments.
extern "C" std::uint64_t gt4_orig_run(const std::uint32_t *args, int index) {
    return run_original(args, g_hooks[index].addr, g_hooks[index].sig);
}
extern "C" std::uint64_t gt4_orig_run_addr(const std::uint32_t *args, std::uint32_t addr) {
    return run_original(args, addr, sig_of(addr));
}
// The same with the parameter kinds given (i, w, f, q) and the return kind (v, i, w, f): tools/eeref/difftest.cpp.
extern "C" std::uint64_t gt4_orig_run_kinds(const std::uint32_t *args, std::uint32_t addr, const char *kinds, char ret) {
    Sig s;
    std::strncpy(s.kinds, kinds, sizeof s.kinds - 1);
    s.kinds[sizeof s.kinds - 1] = 0;
    s.ret = ret;
    return run_original(args, addr, s);
}

// gt4_orig_entry: entered in place of a GT4_ORIG function's native code, with %ecx = its hook index.
// gt4_orig_entry_addr: entered from a PS2 address's trampoline (GT4_ORIG=all), with %ecx = that address.
__asm__(".text\n"
        ".globl _gt4_orig_entry\n"
        "_gt4_orig_entry:\n"
        "  pushl %ecx\n"
        "  leal 8(%esp), %eax\n"
        "  pushl %eax\n"
        "  call _gt4_orig_run\n"
        "  addl $8, %esp\n"
        "  ret\n"
        ".globl _gt4_orig_entry_addr\n"
        "_gt4_orig_entry_addr:\n"
        "  pushl %ecx\n"
        "  leal 8(%esp), %eax\n"
        "  pushl %eax\n"
        "  call _gt4_orig_run_addr\n"
        "  addl $8, %esp\n"
        "  ret\n");
extern "C" void gt4_orig_entry();
extern "C" void gt4_orig_entry_addr();

namespace {

LONG CALLBACK on_break(EXCEPTION_POINTERS *e) {
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_BREAKPOINT) return EXCEPTION_CONTINUE_SEARCH;
    for (int k = 0; k < g_hook_count; ++k)
        if (e->ExceptionRecord->ExceptionAddress == g_hooks[k].fn) {
            e->ContextRecord->Ecx = static_cast<DWORD>(k);
            e->ContextRecord->Eip = static_cast<DWORD>(reinterpret_cast<uintptr_t>(&gt4_orig_entry));
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    return EXCEPTION_CONTINUE_SEARCH;
}

void load_sigs() {
    const char *path = getenv("GT4_ORIG_KINDS");
    FILE *f = path ? std::fopen(path, "r") : nullptr;
    if (!f) {
        gt4::rt::log_info("orig: no GT4_ORIG_KINDS table; every function takes eight integers and returns one");
        return;
    }
    char line[128], kinds[64], ret[8];
    unsigned addr;
    while (std::fgets(line, sizeof line, f))
        if (std::sscanf(line, "%x %63s %7s", &addr, kinds, ret) == 3) {
            Sig s;
            std::snprintf(s.kinds, sizeof s.kinds, "%s", kinds[0] == '-' ? "" : kinds);
            s.ret = ret[0];
            g_sigs[addr] = s;
        }
    std::fclose(f);
    gt4::rt::log_info("orig: %u function signatures from %s", static_cast<unsigned>(g_sigs.size()), path);
}

// GT4_ORIG=all: every PS2 function address whose trampoline leads to native game code leads to its original
// code instead, through a 10-byte thunk (mov $addr, %ecx; jmp gt4_orig_entry_addr). So do the runtime's
// stand-ins for game functions without a matched source (gt4_replace_func_*: runtime/pending.cpp, whose
// 004B01F8 would load an EE jmp_buf into the x86 registers through the native longjmp, and the VU0 helpers of
// runtime/native/), except those that drive the hardware or the host (kHardware). The other runtime functions
// keep their trampolines: the SDK's hardware layer stays native.
void redirect_all() {
    static const std::uint32_t kHardware[] = {
        0x0049FF38, 0x004A1140, 0x004AB430, 0x004AB930, 0x004ABA50,  // runtime/gs.cpp: DMA starts, GS readback
        0x0054E3D8,                                                  // runtime/native/movie.cpp: the IPU
        0x005B72F8,                                                  // runtime/sdk.cpp
        0x0057F188,                                                  // runtime/libc.cpp
    };
    const auto end = reinterpret_cast<std::uintptr_t>(&gt4_game_text_end);
    std::unordered_map<std::uint32_t, bool> pending;
    for (std::uint32_t k = 0; k < gt4_native_function_count; ++k)
        if (std::strncmp(gt4_native_functions[k].name, "gt4_replace_func_", 17) == 0) pending[gt4_native_functions[k].addr] = true;
    for (std::uint32_t a : kHardware) pending.erase(a);
    // GT4_ORIG_KEEP=ADDR|LO-HI,...: functions that stay native (a test hook such as the GT4_MONEY floor,
    // 0043AF68; address ranges, HI excluded, to bisect a difference between the native and the original code)
    std::unordered_map<std::uint32_t, bool> keep;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> keepRanges;
    for (const char *k = getenv("GT4_ORIG_KEEP"); k && *k;) {
        char *e;
        const auto lo = static_cast<std::uint32_t>(std::strtoul(k, &e, 16));
        if (e == k) break;
        if (*e == '-') {
            const char *h = e + 1;
            keepRanges.emplace_back(lo, static_cast<std::uint32_t>(std::strtoul(h, &e, 16)));
        } else {
            keep[lo] = true;
        }
        k = *e == ',' ? e + 1 : e;
    }
    for (std::uint32_t k = 0; k < gt4_guest_function_count; ++k)
        for (const auto &[lo, hi] : keepRanges)
            if (gt4_guest_functions[k].addr >= lo && gt4_guest_functions[k].addr < hi) keep[gt4_guest_functions[k].addr] = true;
    for (const auto &[a, _] : keep) pending.erase(a);
    auto *thunks = static_cast<std::uint8_t *>(
        VirtualAlloc(nullptr, gt4_guest_function_count * 10u, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    std::uint32_t count = 0;
    for (std::uint32_t k = 0; k < gt4_guest_function_count; ++k) {
        const std::uint32_t addr = gt4_guest_functions[k].addr;
        auto *p = reinterpret_cast<std::uint8_t *>(static_cast<uintptr_t>(addr));
        if (p[0] != 0xE9) continue;
        std::int32_t rel;
        std::memcpy(&rel, p + 1, 4);
        if (addr + 5 + static_cast<std::uintptr_t>(rel) >= end && !pending.count(addr)) continue;
        if (keep.count(addr)) continue;
        std::uint8_t *t = thunks + count * 10;
        t[0] = 0xB9;  // mov $addr, %ecx
        std::memcpy(t + 1, &addr, 4);
        t[5] = 0xE9;  // jmp gt4_orig_entry_addr
        const std::int32_t to_entry = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(&gt4_orig_entry_addr) -
                                                                reinterpret_cast<std::uintptr_t>(t + 10));
        std::memcpy(t + 6, &to_entry, 4);
        const std::int32_t to_thunk = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(t) - (addr + 5));
        std::memcpy(p + 1, &to_thunk, 4);
        ++count;
    }
    g_thunks = reinterpret_cast<std::uintptr_t>(thunks);
    g_thunks_end = g_thunks + count * 10u;
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    gt4::rt::log_info("orig: all game code is the original's (%u PS2 entry points redirected)", count);
}

}  // namespace

// Called by main (runtime/main.cpp) after the trampolines are in place; without --eeorig the link supplies an
// empty one.
extern "C" void gt4_difftest(const char *names);  // difftest.cpp

extern "C" void gt4_orig_install() {
    if (const char *t = getenv("GT4_DIFFTEST")) {   // native against original on random inputs, then exit
        register_generated_code();
        load_sigs();
        gt4_difftest(t);
    }
    const char *spec = getenv("GT4_ORIG");
    if (!spec || !*spec) return;
    register_generated_code();
    load_sigs();
    if (std::strcmp(spec, "all") == 0) {
        redirect_all();
        return;
    }
    const char *p = spec;
    while (*p && g_hook_count < kMaxHooks) {
        Hook &h = g_hooks[g_hook_count];
        char *end;
        h.addr = static_cast<std::uint32_t>(std::strtoul(p, &end, 16));
        h.sig = sig_of(h.addr);
        p = end;
        if (*p == '=') {
            int n = 0;
            for (++p; *p && *p != ':' && *p != ','; ++p)
                if (n < 23) h.sig.kinds[n++] = *p;
            h.sig.kinds[n] = 0;
        }
        if (*p == ':') {
            h.sig.ret = p[1];
            p += 2;
        }
        if (*p == ',') ++p;
        for (std::uint32_t k = 0; k < gt4_native_function_count; ++k)
            if (gt4_native_functions[k].addr == h.addr) h.fn = static_cast<std::uint8_t *>(gt4_native_functions[k].fn);
        if (!h.fn) {
            gt4::rt::log_error("orig: no native function at %08X", h.addr);
            continue;
        }
        DWORD old;
        VirtualProtect(h.fn, 1, PAGE_EXECUTE_READWRITE, &old);
        *h.fn = 0xCC;
        VirtualProtect(h.fn, 1, old, &old);
        FlushInstructionCache(GetCurrentProcess(), h.fn, 1);
        gt4::rt::log_info("orig: %08X runs the original code (arguments %s, return %c)", h.addr, h.sig.kinds, h.sig.ret);
        ++g_hook_count;
    }
    AddVectoredExceptionHandler(1, on_break);
}
