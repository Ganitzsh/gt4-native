// eeref: replays a recorded call of a game function (runtime/record.cpp, GT4_RECORD) on the original MIPS
// code, translated to C++ by ps2-web-recomp's eerecomp (third_party/ps2-web-recomp), and compares the
// result with what the native function left.
//
//   eeref PRE [--args KINDS] [--elf CORE.GT4.elf] [--syms symbol_addrs.txt] [--all] [--max N]
//
// PRE is ADDR_N.pre; ADDR_N.post is read next to it. KINDS lists the native function's parameters as the
// caller's stack holds them, comma-separated: i (32-bit integer or pointer), w (64-bit integer, two stack
// words), f (float). They go to the EE's registers as gcc 2.9 for the EE passes them: integers in
// $a0-$a3, $t0-$t3, floats in $f12-$f19. The default is four i.
//
// Both sides start from the same guest RAM, scratchpad, VU0 memory and VU0 registers. The original runs
// with its stack outside RAM (0x02000000), so every difference left in RAM is the two functions'. A word
// that both changed to floats within 1e-5 of each other counts as rounding (the EE's FPU truncates and
// has no denormals; the x87 does neither) and is listed only with --all.
#include "ps2/generated.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <map>
#include <string>
#include <vector>

void register_generated_code();

// The bridge's hook at function entries (tools/eeref/hook_entries.py): the replay runs everything translated.
bool gt4_orig_native(ps2::Cpu &, std::uint32_t) { return false; }

namespace ps2 {

alignas(64) std::uint8_t g_ram[kRamSize];
alignas(64) std::uint8_t g_spr[kScratchpadSize];

namespace {

constexpr std::uint32_t kStackBase = 0x02000000u, kStackSize = 1u << 20;
constexpr std::uint32_t kVuMem = 0x11000000u, kVuMemSize = 0x5000u;
constexpr std::uint32_t kReturn = 0x00001000u;  // $ra of the replayed call
std::uint8_t g_stack[kStackSize];
std::uint8_t g_vu_mem[kVuMemSize];
std::vector<std::uint8_t> g_native_stack;  // the caller's native stack window: arguments point at its locals
std::uint32_t g_native_stack_base;
bool g_halted;
int g_bad_accesses;

std::uint8_t *slow_ptr(std::uint32_t a, std::uint32_t n) {
    if (a - g_native_stack_base <= g_native_stack.size() - n && g_native_stack.size() >= n)
        return g_native_stack.data() + (a - g_native_stack_base);
    if (a - kStackBase <= kStackSize - n) return g_stack + (a - kStackBase);
    if (a - kVuMem <= kVuMemSize - n) return g_vu_mem + (a - kVuMem);
    const std::uint32_t m = a & 0x0FFFFFFFu;  // uncached mirrors at 0x20000000 and 0x30000000
    if ((a >> 28) >= 2 && (a >> 28) <= 3 && m <= kRamSize - n) return g_ram + m;
    if (++g_bad_accesses <= 8) std::fprintf(stderr, "eeref: access to unmapped %08X\n", a);
    return nullptr;
}

std::vector<UnitInfo> g_units;
std::uint32_t g_origin, g_unit_size = 1;
std::vector<const UnitInfo *> g_index;

}  // namespace

std::uint8_t slow_ld8(std::uint32_t a) { auto *p = slow_ptr(a, 1); return p ? *p : 0; }
std::uint16_t slow_ld16(std::uint32_t a) { auto *p = slow_ptr(a, 2); return p ? load<std::uint16_t>(p) : 0; }
std::uint32_t slow_ld32(std::uint32_t a) { auto *p = slow_ptr(a, 4); return p ? load<std::uint32_t>(p) : 0; }
std::uint64_t slow_ld64(std::uint32_t a) { auto *p = slow_ptr(a, 8); return p ? load<std::uint64_t>(p) : 0; }
void slow_ld128(std::uint32_t a, Reg128 &out) {
    if (auto *p = slow_ptr(a, 16)) std::memcpy(&out, p, 16);
    else std::memset(&out, 0, 16);
}
void slow_st8(std::uint32_t a, std::uint8_t v) { if (auto *p = slow_ptr(a, 1)) *p = v; }
void slow_st16(std::uint32_t a, std::uint16_t v) { if (auto *p = slow_ptr(a, 2)) store(p, v); }
void slow_st32(std::uint32_t a, std::uint32_t v) { if (auto *p = slow_ptr(a, 4)) store(p, v); }
void slow_st64(std::uint32_t a, std::uint64_t v) { if (auto *p = slow_ptr(a, 8)) store(p, v); }
void slow_st128(std::uint32_t a, const Reg128 &v) { if (auto *p = slow_ptr(a, 16)) std::memcpy(p, &v, 16); }

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
    std::fprintf(stderr, "eeref: stopped at %08X: %s\n", pc, what);
    g_halted = true;
    c.pc = pc | kUnwind;
}

void dispatch(Cpu &c, std::uint32_t, std::uint32_t depth) {
    const std::uint32_t pc = c.pc;
    if ((pc & kUnwind) || pc == kReturn || g_halted) return;
    const std::uint32_t slot = (pc - g_origin) / g_unit_size;
    const UnitInfo *u = pc >= g_origin && slot < g_index.size() ? g_index[slot] : nullptr;
    const std::uint16_t id = u ? u->entries[(pc - u->base) >> 2] : 0;
    if (!id) {
        std::fprintf(stderr, "eeref: no translated code at %08X (ra %08X)\n", pc, c.r[31].w[0]);
        stop(c, pc, "missing code");
        return;
    }
    u->function(c, id, depth);
}

void call(Cpu &c, std::uint32_t target, std::uint32_t depth) {
    c.pc = target;
    if (depth >= kMaxDepth) return;
    dispatch(c, 0, depth);
}

void bad_entry(Cpu &c, std::uint32_t unit_base) {
    std::fprintf(stderr, "eeref: bad entry into unit %08X for %08X\n", unit_base, c.pc);
    stop(c, c.pc, "bad entry");
}
bool preempt(Cpu &c) {
    c.budget = 1 << 30;
    return false;
}
void syscall(Cpu &c, std::uint32_t pc) {
    std::fprintf(stderr, "eeref: syscall %d at %08X\n", static_cast<int>(c.r[3].w[0]), pc);
    stop(c, pc, "syscall");
}
void breakpoint(Cpu &c, std::uint32_t pc) { stop(c, pc, "break"); }
void trap(Cpu &c, std::uint32_t pc) { stop(c, pc, "trap"); }
void probe(Cpu &, std::uint32_t) {}

std::uint32_t mfc0(Cpu &c, std::uint32_t reg, std::uint32_t) { return reg == 12 ? 0x10001u : c.cop0[reg & 31]; }
void mtc0(Cpu &c, std::uint32_t reg, std::uint32_t value) { c.cop0[reg & 31] = value; }
void set_interrupts(Cpu &, bool) {}
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
void vcallms(Cpu &c, std::uint32_t address) {
    std::fprintf(stderr, "eeref: vcallms %04X (VU0 microprograms are not replayed yet)\n", address);
    stop(c, c.pc, "vcallms");
}
bool vu0_busy(Cpu &) { return false; }
void vu0_wait(Cpu &) {}
std::uint8_t *vu0_data() { return g_vu_mem + 0x4000; }

}  // namespace ps2

namespace {

using namespace ps2;

struct RecHeader {
    char magic[8];
    std::uint32_t addr, index, vblank, kind;
    std::uint32_t args[16];
    std::uint32_t eax, edx;
    std::uint32_t ram_size, spr_size, vu_size, vu0_state_size;
    std::uint32_t stack_base, stack_size;
    std::uint32_t ram_pages;  // .post: only the pages of RAM that changed (address, 4 KB each)
};

// runtime/native/vu0.h, Gt4Vu0State
struct NativeVu0 {
    float vf[32][4];
    float acc[4];
    float q, i, p, pad0;
    std::uint32_t r, clip;
    std::uint32_t vi[32];
};

struct State {
    RecHeader h{};
    NativeVu0 vu0{};
    std::vector<std::uint8_t> ram, spr, vu, stack;
};

// A .pre file, or a .post file over a copy of its .pre's RAM (base).
bool read_state(const std::string &path, State &s, const std::vector<std::uint8_t> *base = nullptr) {
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) {
        std::fprintf(stderr, "eeref: cannot open %s\n", path.c_str());
        return false;
    }
    bool ok = std::fread(&s.h, sizeof s.h, 1, f) == 1 && !std::memcmp(s.h.magic, "GT4REC3", 8) &&
              s.h.vu0_state_size == sizeof(NativeVu0) && s.h.ram_size == kRamSize && s.h.spr_size == kScratchpadSize &&
              s.h.vu_size == 0x5000u && std::fread(&s.vu0, sizeof s.vu0, 1, f) == 1;
    if (ok) {
        s.spr.resize(s.h.spr_size);
        s.vu.resize(s.h.vu_size);
        s.stack.resize(s.h.stack_size);
        if (s.h.kind == 0) {
            s.ram.resize(s.h.ram_size);
            ok = std::fread(s.ram.data(), 1, s.ram.size(), f) == s.ram.size();
        } else {
            s.ram = *base;
            for (std::uint32_t k = 0; ok && k < s.h.ram_pages; ++k) {
                std::uint32_t a = 0;
                ok = std::fread(&a, 4, 1, f) == 1 && a <= kRamSize - 0x1000u && std::fread(s.ram.data() + a, 1, 0x1000, f) == 0x1000;
            }
        }
        ok = ok && std::fread(s.spr.data(), 1, s.spr.size(), f) == s.spr.size() &&
             std::fread(s.vu.data(), 1, s.vu.size(), f) == s.vu.size() &&
             std::fread(s.stack.data(), 1, s.stack.size(), f) == s.stack.size();
    }
    std::fclose(f);
    if (!ok) std::fprintf(stderr, "eeref: %s is not a GT4_RECORD state of this format\n", path.c_str());
    return ok;
}

// The ELF's code sections (SHF_EXECINSTR) over a RAM image: the native process has its trampolines there,
// and code is sometimes read as data. Only sections: the segment that holds .text holds .data too.
void load_text(const std::string &path, std::initializer_list<std::uint8_t *> rams) {
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) {
        std::fprintf(stderr, "eeref: cannot open %s\n", path.c_str());
        return;
    }
    std::vector<std::uint8_t> elf;
    std::uint8_t buf[1 << 16];
    for (std::size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) elf.insert(elf.end(), buf, buf + n);
    std::fclose(f);
    auto u32 = [&](std::size_t o) { return load<std::uint32_t>(elf.data() + o); };
    auto u16 = [&](std::size_t o) { return load<std::uint16_t>(elf.data() + o); };
    const std::uint32_t shoff = u32(0x20), shentsize = u16(0x2E), shnum = u16(0x30);
    for (std::uint32_t k = 0; k < shnum; ++k) {
        const std::size_t sh = shoff + k * shentsize;
        if (u32(sh + 4) != 1 || !(u32(sh + 8) & 4)) continue;  // SHT_PROGBITS, SHF_EXECINSTR
        const std::uint32_t addr = u32(sh + 12), off = u32(sh + 16), size = u32(sh + 20);
        if (addr < kRamSize && addr + size <= kRamSize)
            for (std::uint8_t *ram : rams) std::memcpy(ram + addr, elf.data() + off, size);
    }
}

struct Symbol {
    std::uint32_t addr;
    std::string name;
};
std::vector<Symbol> g_syms;

void load_syms(const std::string &path) {
    FILE *f = std::fopen(path.c_str(), "r");
    if (!f) return;
    char line[512];
    while (std::fgets(line, sizeof line, f)) {
        char name[256];
        unsigned addr;
        if (std::sscanf(line, " %255[A-Za-z0-9_] = 0x%x", name, &addr) == 2) g_syms.push_back({addr, name});
    }
    std::fclose(f);
    std::sort(g_syms.begin(), g_syms.end(), [](const Symbol &a, const Symbol &b) { return a.addr < b.addr; });
}

std::string where(std::uint32_t a, const std::uint32_t *args) {
    char out[160];
    int best = -1;
    for (int k = 0; k < 4; ++k)
        if (args[k] >= 0x00100000u && args[k] < kRamSize && a >= args[k] && a - args[k] < 0x20000u &&
            (best < 0 || args[k] > args[best]))
            best = k;
    if (best >= 0) {
        std::snprintf(out, sizeof out, "arg%d+0x%X", best, a - args[best]);
        return out;
    }
    auto it = std::upper_bound(g_syms.begin(), g_syms.end(), a, [](std::uint32_t v, const Symbol &s) { return v < s.addr; });
    if (it != g_syms.begin() && a - std::prev(it)->addr < 0x10000u) {
        std::snprintf(out, sizeof out, "%s+0x%X", std::prev(it)->name.c_str(), a - std::prev(it)->addr);
        return out;
    }
    return "";
}

float as_float(std::uint32_t w) {
    float f;
    std::memcpy(&f, &w, 4);
    return f;
}

// A float-looking word: a normal number of moderate size (not an address or small integer).
bool floaty(std::uint32_t w) {
    const std::uint32_t e = (w >> 23) & 0xFF;
    return w == 0 || w == 0x80000000u || (e > 90 && e < 170);
}

bool close(std::uint32_t a, std::uint32_t b) {
    if (!floaty(a) || !floaty(b)) return false;
    const double x = as_float(a), y = as_float(b);
    return std::fabs(x - y) <= 1e-5 * std::max(std::fabs(x), std::fabs(y)) + 1e-12;
}

struct Diff {
    std::uint32_t addr, pre, native, ref;
};

}  // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: eeref ADDR_N.pre [--args i,f,w,q...] [--ret v|i|w|f] [--elf CORE.GT4.elf] [--syms FILE] [--all] "
                             "[--max N]\n");
        return 2;
    }
    std::string pre_path = argv[1], kinds = "i,i,i,i", elf, syms;
    char ret_kind = 0;  // v none, i 32-bit, w 64-bit, f float (natively in eax); 0: not compared
    bool all = false;
    int max_lines = 60;
    for (int k = 2; k < argc; ++k) {
        const std::string a = argv[k];
        if (a == "--args" && k + 1 < argc) kinds = argv[++k];
        else if (a == "--ret" && k + 1 < argc) ret_kind = argv[++k][0];
        else if (a == "--elf" && k + 1 < argc) elf = argv[++k];
        else if (a == "--syms" && k + 1 < argc) syms = argv[++k];
        else if (a == "--max" && k + 1 < argc) max_lines = std::atoi(argv[++k]);
        else if (a == "--all") all = true;
    }
    std::string post_path = pre_path;
    if (post_path.size() > 4 && post_path.compare(post_path.size() - 4, 4, ".pre") == 0)
        post_path.replace(post_path.size() - 4, 4, ".post");
    State pre, post;
    if (!read_state(pre_path, pre) || !read_state(post_path, post, &pre.ram)) return 2;
    if (!syms.empty()) load_syms(syms);

    register_generated_code();
    std::memcpy(g_ram, pre.ram.data(), kRamSize);
    std::memcpy(g_spr, pre.spr.data(), kScratchpadSize);
    std::memcpy(g_vu_mem, pre.vu.data(), kVuMemSize);
    g_native_stack = pre.stack;
    g_native_stack_base = pre.h.stack_base;
    if (!elf.empty()) load_text(elf, {g_ram, pre.ram.data(), post.ram.data()});

    static Cpu c{};
    for (int k = 0; k < 32; ++k)
        for (int l = 0; l < 4; ++l) std::memcpy(&c.vu0.vf[k].w[l], &pre.vu0.vf[k][l], 4);
    std::memcpy(c.vu0.acc.w, pre.vu0.acc, 16);
    for (int k = 0; k < 16; ++k) c.vu0.vi[k] = static_cast<std::uint16_t>(pre.vu0.vi[k]);
    std::memcpy(&c.vu0.q, &pre.vu0.q, 4);
    std::memcpy(&c.vu0.i, &pre.vu0.i, 4);
    std::memcpy(&c.vu0.p, &pre.vu0.p, 4);
    c.vu0.r = pre.vu0.r & 0x7FFFFFu;
    c.vu0.clip = pre.vu0.clip;
    c.vu0.status = pre.vu0.vi[16];
    c.vu0.mac = pre.vu0.vi[17];

    // the arguments: native stack words to EE registers
    std::uint32_t word = 0, ireg = 4, freg = 12;
    for (std::size_t at = 0; at < kinds.size(); at += 2) {
        const char kind = kinds[at];
        if (kind == 'f') {
            if (freg <= 19) c.f[freg++] = pre.h.args[word];
            word += 1;
        } else if (kind == 'w') {
            if (ireg <= 11) c.r[ireg++].d[0] = pre.h.args[word] | static_cast<std::uint64_t>(pre.h.args[word + 1]) << 32;
            word += 2;
        } else if (kind == 'q') {
            if (ireg <= 11) std::memcpy(c.r[ireg++].w, &pre.h.args[word], 16);
            word += 4;
        } else {
            if (ireg <= 11) c.r[ireg++].sd[0] = static_cast<std::int32_t>(pre.h.args[word]);
            word += 1;
        }
    }
    c.r[28].sd[0] = 0x006DDDF0;  // $gp (crt0)
    c.r[29].sd[0] = static_cast<std::int32_t>(kStackBase + kStackSize - 0x100);
    c.r[31].sd[0] = kReturn;
    c.budget = 1 << 30;
    c.pc = pre.h.addr;
    for (int round = 0; round < 1000 && !g_halted && c.pc != kReturn; ++round) {
        c.pc &= ~kUnwind;
        dispatch(c, 0, 0);
    }
    const bool returned = c.pc == kReturn && !g_halted;

    std::printf("%08X call %u (VBlank %u): original %s\n", pre.h.addr, pre.h.index, pre.h.vblank,
                returned ? "returned" : "did NOT return");
    std::printf("  return: native eax %08X edx %08X | original v0 %016llX v1 %08X f0 %08X (%g)\n", post.h.eax, post.h.edx,
                static_cast<unsigned long long>(c.r[2].d[0]), c.r[3].w[0], c.f[0], as_float(c.f[0]));
    bool ret_differs = false;
    if (ret_kind == 'i') ret_differs = post.h.eax != c.r[2].w[0];
    if (ret_kind == 'w') ret_differs = post.h.eax != c.r[2].w[0] || post.h.edx != c.r[2].w[1];
    if (ret_kind == 'f') ret_differs = post.h.eax != c.f[0] && !close(post.h.eax, c.f[0]);
    if (ret_differs) std::printf("  RETURN DIFFERS (%c)\n", ret_kind);

    // memory: every word either side changed
    std::vector<Diff> diffs;
    std::size_t same_changed = 0, rounding = 0, native_only = 0, ref_only = 0;
    auto scan = [&](const std::uint8_t *p0, const std::uint8_t *pn, const std::uint8_t *pr, std::uint32_t base, std::uint32_t size) {
        for (std::uint32_t o = 0; o < size; o += 4) {
            const std::uint32_t a = load<std::uint32_t>(p0 + o), n = load<std::uint32_t>(pn + o), r = load<std::uint32_t>(pr + o);
            if (n == a && r == a) continue;
            if (n == r) {
                ++same_changed;
                continue;
            }
            const bool round_off = close(n, r);
            rounding += round_off;
            native_only += r == a;
            ref_only += n == a;
            if (!round_off || all) diffs.push_back({base + o, a, n, r});
        }
    };
    scan(pre.ram.data() + 0x100000, post.ram.data() + 0x100000, g_ram + 0x100000, 0x100000, kRamSize - 0x100000);
    scan(pre.spr.data(), post.spr.data(), g_spr, kScratchpadBase, kScratchpadSize);
    scan(pre.vu.data(), post.vu.data(), g_vu_mem, kVuMem, kVuMemSize);
    if (post.stack.size() == g_native_stack.size())  // the arguments are the first 64 bytes: not compared
        scan(pre.stack.data() + 64, post.stack.data() + 64, g_native_stack.data() + 64, g_native_stack_base + 64,
             static_cast<std::uint32_t>(g_native_stack.size()) - 64);
    std::printf("  memory: %zu words changed alike, %zu within float rounding, %zu differ (%zu only native changed, %zu only "
                "the original changed)\n",
                same_changed, rounding, diffs.size() - (all ? rounding : 0), native_only, ref_only);
    int shown = 0;
    for (const Diff &d : diffs) {
        if (shown++ >= max_lines) {
            std::printf("  ... %zu more\n", diffs.size() - max_lines);
            break;
        }
        std::printf("  %08X %-28s pre %08X native %08X original %08X", d.addr, where(d.addr, pre.h.args).c_str(), d.pre, d.native,
                    d.ref);
        if (floaty(d.native) || floaty(d.ref)) std::printf("   (%g / %g)", as_float(d.native), as_float(d.ref));
        std::printf("\n");
    }

    // VU0 registers
    int vu_diffs = 0;
    for (int k = 1; k < 32; ++k)
        for (int l = 0; l < 4; ++l) {
            std::uint32_t n, r = c.vu0.vf[k].w[l];
            std::memcpy(&n, &post.vu0.vf[k][l], 4);
            if (n != r && !close(n, r) && vu_diffs++ < 16)
                std::printf("  vf%02d.%c native %g original %g\n", k, "xyzw"[l], as_float(n), as_float(r));
        }
    if (vu_diffs) std::printf("  %d VU0 register lanes differ\n", vu_diffs);
    return returned && !ret_differs && diffs.size() == (all ? rounding : 0) && !vu_diffs ? 0 : 1;
}
