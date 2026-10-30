// GT4_GS_WRITERS=FROM,FRAMES[;FROM,FRAMES...] (debugging aid): for each window, which game functions wrote the
// bytes that the DMA channels delivered towards the GS. docs/GRAPHICS_API_PLAN.md ("Measuring") describes the
// report; this comment, how it is measured.
//
// runtime/gs.cpp feeds every DMA tag the channels read and every byte they deliver, in order, with its guest
// address. A VIF parser (VIF0/VIF1: codes, UNPACK destinations, MPG, DIRECT) and a GIF parser per path (2: VIF1
// DIRECT, 3: the GIF channel) give each word a category (DMA tag, VIF code, batch vertex data, a batch's GIF tag,
// constants and GS state packet, GIF tags, A+D writes by register group, PACKED and REGLIST vertices, IMAGE
// pixels). Each word's writer comes from a shadow map of guest memory (one site per 32-bit word): during a
// window the pages that held packet data are write-protected (RAM's main view, or the scratchpad); a write
// faults into a vectored handler that names the writing instruction's game function (native_name_for, and its
// caller from the frame pointer; for runtime or C runtime code, the game functions on the stack), opens the
// page, single-steps the instruction, records the words it wrote (the store's operand size, and any word that
// changed) and protects the page again. A window opens at VBlank FROM and goes by frames (a frame: the VIF1
// chain 004A11F8 kicks): two frames learning the pages (nothing protected), two warming the shadow, then FRAMES
// frames measured. The faults slow the game about tenfold; the time they cost is taken off the game's bus clock
// (kernel_add_stall), which keeps VBlank-driven game logic closer to its usual pace (not exactly: a measured
// frame still spans several VBlanks, and loading, which runs in real time, gets ahead).
// GT4_GS_WRITERS_IMAGE=1 also watches the pages of IMAGE data (texture pixels: many writes);
// GT4_GS_WRITERS_DIR: where the reports go (default: the working directory), gs_writers_<FROM>.txt.
//
// Off (the variable unset) nothing here runs: gs.cpp tests g_on. While a window is open the disc is read through
// a copy (iop.cpp), since ReadFile into a protected page fails instead of faulting.
//
// One window per run: guest RAM is the executable's shared .guest section, and changing the protection of one of
// its pages gives the main view a private copy of that page; the mirrors (0x20000000, 0x30000000) keep reading
// the shared one, so after a window the game and the runtime see two memories (measured: about 100 pages differ
// a few VBlanks after a window ends, and the next GS -> EE download, whose list 004AB930 reads through the
// mirror, wrote to address 0). The window itself is measured before that matters; later windows of the same
// run, and the rest of the run, are not reliable (the tool says so in the log).
#include "gs_writers.h"

#include "guest.h"
#include "image.h"
#include "kernel.h"
#include "log.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

extern "C" void gt4_game_text_end();

namespace gt4::rt::gsw {

bool g_on = false;

namespace {

constexpr uint32_t kRamPages = kRamSize >> 12;
constexpr uint32_t kSprPages = kScratchpadSize >> 12;
constexpr uint32_t kPages = kRamPages + kSprPages;
constexpr uint32_t kElfStart = 0x00100000, kElfEnd = 0x006D6000;  // the image's loaded segments

// ---------------------------------------------------------------------------------------------
// Sites: (writer, caller, kind), interned in a lock-free table; the index is the id the shadow keeps.

enum Kind : uint32_t {
    kDirect = 0,      // game code wrote it
    kViaRuntime = 1,  // runtime code called from game code (writer: the nearest game function on the stack)
    kViaCrt = 2,      // the C runtime (memcpy, memset) or the system, called from game code
    kRefBy = 3,       // on a page not watched: data a ref tag delivered; writer = the tag's writer
    kStale = 4,       // on a watched page, not written in the window; writer = the page, caller = the tag's writer
    kStatic = 5,      // the executable's data as loaded; writer = the page, caller = the tag's writer
    kUnwatched = 6,   // on a page not watched (texture pixels, microcode...); writer = the page
    kElsewhere = 7,   // outside guest RAM and the scratchpad (a native stack)
};
const char *const kKindNames[] = {"direct", "via-runtime", "via-crt", "ref-by", "stale", "static", "unwatched", "elsewhere"};

constexpr uint32_t kSiteSlots = 1u << 16;
volatile LONG64 *g_sites;  // key = writer | kind << 28 | 1 << 31 | caller << 32; 0 = empty
uint16_t *g_shadow;        // per guest word (RAM, then the scratchpad): the site that last wrote it
uint8_t *g_elf_copy;       // the image's data as loaded

uint64_t load_key(uint16_t id) { return static_cast<uint64_t>(InterlockedCompareExchange64(&g_sites[id], 0, 0)); }

uint16_t site_id(uint32_t writer, uint32_t caller, uint32_t kind) {
    const uint64_t key = (static_cast<uint64_t>(caller) << 32) | 0x80000000u | (kind << 28) | (writer & 0x0FFFFFFF);
    uint32_t h = static_cast<uint32_t>((key * 0x9E3779B97F4A7C15ull) >> 48);
    for (uint32_t probe = 0; probe < kSiteSlots; ++probe, h = (h + 1) & (kSiteSlots - 1)) {
        if (h == 0) continue;
        const uint64_t cur = load_key(static_cast<uint16_t>(h));
        if (cur == key) return static_cast<uint16_t>(h);
        if (cur == 0) {
            const uint64_t prev = static_cast<uint64_t>(
                InterlockedCompareExchange64(&g_sites[h], static_cast<LONG64>(key), 0));
            if (prev == 0 || prev == key) return static_cast<uint16_t>(h);
        }
    }
    return 0;
}
uint32_t site_kind(uint16_t id) { return static_cast<uint32_t>(load_key(id) >> 28) & 7; }
uint32_t site_writer(uint16_t id) { return static_cast<uint32_t>(load_key(id)) & 0x0FFFFFFF; }
uint32_t site_caller(uint16_t id) { return static_cast<uint32_t>(load_key(id) >> 32); }
bool site_real(uint16_t id) { return id && site_kind(id) <= kViaCrt; }

// ---------------------------------------------------------------------------------------------
// Guest pages: the RAM's three views (0, 0x20000000, 0x30000000) and the scratchpad.

uint8_t g_want[kPages];  // held packet data this window
uint8_t g_prot[kPages];  // protected now
uint8_t g_ever[kPages];  // protected at some point (a late fault restores it)
uint8_t g_win[kPages];   // protected in this window
DWORD g_old[3][kPages];  // each view's protection before the first protect
volatile LONG g_watching = 0;
volatile LONG g_faults = 0, g_lost = 0, g_failed_pages = 0;
bool g_watch_image = false;

uintptr_t view_addr(int view, uint32_t page) {
    if (page >= kRamPages) return kScratchpad + (page - kRamPages) * 4096u;
    const uintptr_t base = view == 0 ? 0 : view == 1 ? kUncachedMirror : kUncachedAccelMirror;
    return base + page * 4096u;
}
// Only the main view is protected: the mirrors are copy-on-write views of the shared section, and giving a
// page of them its PAGE_EXECUTE_WRITECOPY back made it really copy-on-write (the mirror then read zeros where
// the game had written: a GS -> EE download list read through 0x20000000 crashed 004AB930). Writes through the
// mirrors are therefore not seen (their words count as stale).
int view_count(uint32_t) { return 1; }

// A guest address: its word index in the shadow, its view and the end of its region.
bool locate(uintptr_t a, uint32_t &word, int &view, uintptr_t &end) {
    static const uintptr_t bases[3] = {0, kUncachedMirror, kUncachedAccelMirror};
    for (int v = 0; v < 3; ++v)
        if (a >= bases[v] + kGuestSectionStart && a < bases[v] + kRamSize) {
            view = v;
            word = static_cast<uint32_t>((a - bases[v]) >> 2);
            end = bases[v] + kRamSize;
            return true;
        }
    if (a >= kScratchpad && a < kScratchpad + kScratchpadSize) {
        view = 0;
        word = static_cast<uint32_t>((kRamSize + (a - kScratchpad)) >> 2);
        end = kScratchpad + kScratchpadSize;
        return true;
    }
    return false;
}

DWORD read_only(DWORD p) {
    return (p & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) ? PAGE_EXECUTE_READ
                                                                                                         : PAGE_READONLY;
}

bool protect(uint32_t page) {
    for (int v = 0; v < view_count(page); ++v) {
        void *at = reinterpret_cast<void *>(view_addr(v, page));
        if (!g_old[v][page]) {
            MEMORY_BASIC_INFORMATION mi;
            if (!VirtualQuery(at, &mi, sizeof mi) || mi.State != MEM_COMMIT) return false;
            g_old[v][page] = mi.Protect & 0xFF;
            static bool told = false;
            if (!told) {
                told = true;
                log_info("gs_writers: page %08x protection %08lx", static_cast<unsigned>(view_addr(v, page)), mi.Protect);
            }
            if (g_old[v][page] & (PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY)) {  // see view_count
                g_old[v][page] = 0;
                return false;
            }
        }
        DWORD old;
        if (!VirtualProtect(at, 4096, read_only(g_old[v][page]), &old)) {
            for (int u = 0; u < v; ++u) VirtualProtect(reinterpret_cast<void *>(view_addr(u, page)), 4096, g_old[u][page], &old);
            return false;
        }
    }
    g_ever[page] = 1;
    g_win[page] = 1;
    g_prot[page] = 1;
    return true;
}

void unprotect(uint32_t page) {
    g_prot[page] = 0;
    for (int v = 0; v < view_count(page); ++v) {
        DWORD old;
        VirtualProtect(reinterpret_cast<void *>(view_addr(v, page)), 4096, g_old[v][page], &old);
    }
}

// ---------------------------------------------------------------------------------------------
// The write handler.

uintptr_t g_text_end, g_exe_lo, g_exe_hi;

bool game_pc(uintptr_t pc, uint32_t &guest) {
    if (pc < 0x02000000 || pc >= g_text_end) return false;
    guest = 0;
    const char *n = native_name_for(pc, &guest);
    return *n && guest;
}

uint16_t writer_site(const CONTEXT *c) {
    const NT_TIB *tib = reinterpret_cast<const NT_TIB *>(NtCurrentTeb());
    const uintptr_t lo = reinterpret_cast<uintptr_t>(tib->StackLimit), hi = reinterpret_cast<uintptr_t>(tib->StackBase);
    uint32_t writer = 0, caller = 0, kind = kDirect;
    if (game_pc(c->Eip, writer)) {
        const uintptr_t bp = c->Ebp;
        if (bp >= lo && bp + 8 <= hi && !(bp & 3)) game_pc(reinterpret_cast<const uint32_t *>(bp)[1], caller);
    } else {
        kind = (c->Eip >= g_exe_lo && c->Eip < g_exe_hi) ? kViaRuntime : kViaCrt;
        const auto *sp = reinterpret_cast<const uint32_t *>(c->Esp & ~uintptr_t(3));
        for (int k = 0; k < 4096 && reinterpret_cast<uintptr_t>(sp + k + 1) <= hi; ++k) {
            uint32_t g;
            if (!game_pc(sp[k], g)) continue;
            if (!writer) {
                writer = g;
            } else if (g != writer) {
                caller = g;
                break;
            }
        }
    }
    return site_id(writer, caller, kind);
}

// The bytes an x86 store writes (the forms compiled code uses; 4 when unsure).
uint32_t store_size(const uint8_t *ip) {
    bool o16 = false;
    uint8_t rep = 0;
    for (int i = 0; i < 6; ++i, ++ip) {
        const uint8_t b = *ip;
        if (b == 0x66) o16 = true;
        else if (b == 0xF2 || b == 0xF3) rep = b;
        else if (!(b == 0x2E || b == 0x36 || b == 0x3E || b == 0x26 || b == 0x64 || b == 0x65 || b == 0x67 || b == 0xF0)) break;
    }
    const uint8_t op = ip[0], modrm_reg = (ip[1] >> 3) & 7;
    if (op == 0x0F) {
        const uint8_t op2 = ip[1];
        if (op2 == 0x11) return rep == 0xF3 ? 4 : rep == 0xF2 ? 8 : 16;      // movups/movupd/movss/movsd
        if (op2 == 0x29 || op2 == 0x2B) return 16;                          // movaps/movapd, movntps
        if (op2 == 0x7F || op2 == 0xE7) return (o16 || rep == 0xF3) ? 16 : 8;  // movdqa/movdqu (movq mm)
        if (op2 == 0xD6 || op2 == 0x13 || op2 == 0x17) return 8;           // movq, movlps, movhps
        if (op2 >= 0x90 && op2 <= 0x9F) return 1;                          // setcc
        if (op2 == 0xB0) return 1;
        return o16 ? 2 : 4;
    }
    switch (op) {
    case 0x88: case 0xC6: case 0xA4: case 0xAA: case 0x80: case 0x86: case 0xFE: case 0xC0: case 0xD0:
    case 0xD2: case 0xF6: case 0x00: case 0x08: case 0x10: case 0x18: case 0x20: case 0x28: case 0x30:
        return 1;
    case 0xD9: return modrm_reg == 7 ? 2 : 4;              // fst/fstp m32, fnstcw
    case 0xDD: return modrm_reg == 7 ? 2 : 8;              // fst/fstp m64, fnstsw
    case 0xDB: return modrm_reg == 7 ? 10 : 4;             // fstp m80, fist/fistp m32
    case 0xDF: return modrm_reg == 7 ? 8 : modrm_reg == 6 ? 10 : 2;  // fistp m64, fbstp, fistp m16
    default: return o16 ? 2 : 4;
    }
}

struct Pending {
    uint32_t page, first_word, nwords;
    int view;
    uintptr_t first;
    uint16_t site;
    uint32_t before[8];
};
struct Slot {
    volatile LONG tid;
    int n;
    LONGLONG t0;
    Pending p[4];
};
Slot g_slots[256];

Slot *slot_of(DWORD tid, bool make) {
    for (Slot &s : g_slots)
        if (s.tid == static_cast<LONG>(tid)) return &s;
    if (!make) return nullptr;
    for (Slot &s : g_slots)
        if (s.tid == 0 && InterlockedCompareExchange(&s.tid, static_cast<LONG>(tid), 0) == 0) return &s;
    return nullptr;
}

LONG WINAPI on_write(EXCEPTION_POINTERS *ep) {
    const EXCEPTION_RECORD *er = ep->ExceptionRecord;
    CONTEXT *c = ep->ContextRecord;
    if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        if (er->NumberParameters < 2 || er->ExceptionInformation[0] != 1) return EXCEPTION_CONTINUE_SEARCH;
        const uintptr_t at = er->ExceptionInformation[1];
        uint32_t word;
        int view;
        uintptr_t end;
        if (!locate(at, word, view, end) || view != 0) return EXCEPTION_CONTINUE_SEARCH;
        const uint32_t page = word >> 10;
        if (!g_ever[page]) return EXCEPTION_CONTINUE_SEARCH;
        LARGE_INTEGER t0;
        QueryPerformanceCounter(&t0);
        DWORD old;
        VirtualProtect(reinterpret_cast<void *>(view_addr(view, page)), 4096, g_old[view][page], &old);
        if (!g_watching || !g_prot[page]) return EXCEPTION_CONTINUE_EXECUTION;  // the window closed meanwhile
        Slot *s = slot_of(GetCurrentThreadId(), true);
        if (!s || s->n >= 4) {  // left open; the next VBlank protects it again
            g_prot[page] = 0;
            InterlockedIncrement(&g_lost);
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        Pending &p = s->p[s->n];
        if (s->n++ == 0) s->t0 = t0.QuadPart;
        p.page = page;
        p.view = view;
        p.site = writer_site(c);
        p.first = at & ~uintptr_t(3);
        p.first_word = word;
        p.nwords = static_cast<uint32_t>(std::min<uintptr_t>(8, (end - p.first) / 4));
        const auto *cur = reinterpret_cast<const uint32_t *>(p.first);
        for (uint32_t k = 0; k < p.nwords; ++k) p.before[k] = cur[k];
        const uint32_t size = store_size(reinterpret_cast<const uint8_t *>(c->Eip));
        const uint32_t last = std::min<uint32_t>(word + p.nwords - 1, word + static_cast<uint32_t>(((at & 3) + size - 1) / 4));
        for (uint32_t w = word; w <= last; ++w) g_shadow[w] = p.site;
        c->EFlags |= 0x100;  // trap after the store
        InterlockedIncrement(&g_faults);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (er->ExceptionCode == EXCEPTION_SINGLE_STEP) {
        Slot *s = slot_of(GetCurrentThreadId(), false);
        if (!s || s->n == 0) return EXCEPTION_CONTINUE_SEARCH;
        for (int i = 0; i < s->n; ++i) {
            const Pending &p = s->p[i];
            const auto *now = reinterpret_cast<const uint32_t *>(p.first);
            for (uint32_t k = 0; k < p.nwords; ++k)
                if (now[k] != p.before[k]) g_shadow[p.first_word + k] = p.site;
            if (g_watching && g_prot[p.page]) {
                DWORD old;
                VirtualProtect(reinterpret_cast<void *>(view_addr(p.view, p.page)), 4096, read_only(g_old[p.view][p.page]), &old);
            }
        }
        s->n = 0;
        c->EFlags &= ~0x100u;
        LARGE_INTEGER t1;
        QueryPerformanceCounter(&t1);
        kernel_add_stall((t1.QuadPart - s->t0) * 3 / 2);  // the first exception's dispatch is not timed
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// ---------------------------------------------------------------------------------------------
// Categories and statistics.

enum GifCat { gTag, gAdTexture, gAdTransfer, gAdState, gAdEvent, gAdVertex, gAdOther, gPacked, gReglist, gImage, kGifCats };
enum Cat {
    kDmaTag, kVifCode, kVifMpg, kUnpVertex, kUnpGiftag, kUnpConst, kUnpState, kUnpOther, kVif0,
    kGif2, kGif3 = kGif2 + kGifCats, kCats = kGif3 + kGifCats
};
const char *const kGifCatNames[kGifCats] = {"tag", "ad.texture", "ad.transfer", "ad.state", "ad.event",
                                            "ad.vertex", "ad.other", "packed.vertex", "reglist", "image"};
std::string cat_name(int c) {
    static const char *const base[] = {"dma.tag", "vif.code", "vif.mpg", "batch.vertex", "batch.giftag",
                                       "batch.const", "batch.state", "batch.other", "vif0"};
    if (c < kGif2) return base[c];
    if (c < kGif3) return std::string("path2.") + kGifCatNames[c - kGif2];
    return std::string("path3.") + kGifCatNames[c - kGif3];
}
bool cat_watched(int c) {
    if (c == kVifMpg || c == kVif0) return false;
    if (c == kGif2 + gImage || c == kGif3 + gImage) return g_watch_image;
    return true;
}

const char *reg_name(uint32_t r) {
    switch (r) {
    case 0x00: return "PRIM"; case 0x01: return "RGBAQ"; case 0x02: return "ST"; case 0x03: return "UV";
    case 0x04: return "XYZF2"; case 0x05: return "XYZ2"; case 0x06: return "TEX0_1"; case 0x07: return "TEX0_2";
    case 0x08: return "CLAMP_1"; case 0x09: return "CLAMP_2"; case 0x0A: return "FOG"; case 0x0C: return "XYZF3";
    case 0x0D: return "XYZ3"; case 0x14: return "TEX1_1"; case 0x15: return "TEX1_2"; case 0x16: return "TEX2_1";
    case 0x17: return "TEX2_2"; case 0x18: return "XYOFFSET_1"; case 0x19: return "XYOFFSET_2";
    case 0x1A: return "PRMODECONT"; case 0x1B: return "PRMODE"; case 0x1C: return "TEXCLUT"; case 0x22: return "SCANMSK";
    case 0x34: return "MIPTBP1_1"; case 0x35: return "MIPTBP1_2"; case 0x36: return "MIPTBP2_1"; case 0x37: return "MIPTBP2_2";
    case 0x3B: return "TEXA"; case 0x3D: return "FOGCOL"; case 0x3F: return "TEXFLUSH"; case 0x40: return "SCISSOR_1";
    case 0x41: return "SCISSOR_2"; case 0x42: return "ALPHA_1"; case 0x43: return "ALPHA_2"; case 0x44: return "DIMX";
    case 0x45: return "DTHE"; case 0x46: return "COLCLAMP"; case 0x47: return "TEST_1"; case 0x48: return "TEST_2";
    case 0x49: return "PABE"; case 0x4A: return "FBA_1"; case 0x4B: return "FBA_2"; case 0x4C: return "FRAME_1";
    case 0x4D: return "FRAME_2"; case 0x4E: return "ZBUF_1"; case 0x4F: return "ZBUF_2"; case 0x50: return "BITBLTBUF";
    case 0x51: return "TRXPOS"; case 0x52: return "TRXREG"; case 0x53: return "TRXDIR"; case 0x54: return "HWREG";
    case 0x60: return "SIGNAL"; case 0x61: return "FINISH"; case 0x62: return "LABEL"; case 0x7F: return "NOP";
    default: return "?";
    }
}

int ad_group(uint32_t r) {
    switch (r) {
    case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: case 0x0A: case 0x0C: case 0x0D:
        return gAdVertex;
    case 0x06: case 0x07: case 0x08: case 0x09: case 0x14: case 0x15: case 0x16: case 0x17: case 0x1C:
    case 0x34: case 0x35: case 0x36: case 0x37: case 0x3B: case 0x3F:
        return gAdTexture;
    case 0x50: case 0x51: case 0x52: case 0x53: case 0x54:
        return gAdTransfer;
    case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x22: case 0x3D: case 0x40: case 0x41: case 0x42: case 0x43:
    case 0x44: case 0x45: case 0x46: case 0x47: case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D:
    case 0x4E: case 0x4F:
        return gAdState;
    case 0x60: case 0x61: case 0x62:
        return gAdEvent;
    default:
        return gAdOther;
    }
}

// The part of a register's value the report counts distinct values of (false: not counted).
bool value_kept(uint32_t r, uint64_t &v) {
    switch (r) {
    case 0x00: case 0x1B: v &= 0x7FF; return true;  // PRIM, PRMODE: type and flags
    case 0x06: case 0x07: v &= (0x3Full << 20) | (0x7ull << 34) | (0x1Full << 51); return true;  // PSM TCC TFX CPSM CSM
    case 0x08: case 0x09: v &= 0xF; return true;    // CLAMP: the wrap modes
    case 0x50: v &= (0x3Full << 24) | (0x3Full << 56); return true;  // BITBLTBUF: SPSM, DPSM
    case 0x60: case 0x61: case 0x62: v = 0; return true;
    case 0x14: case 0x15: case 0x1A: case 0x3B: case 0x3D: case 0x42: case 0x43: case 0x45: case 0x46: case 0x47:
    case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F: case 0x52: case 0x53:
        return true;
    default: return false;
    }
}

struct SiteStats {
    uint64_t bytes[kCats] = {};
    uint64_t regs[2] = {};  // A+D registers written (0..127)
    uint32_t prims = 0;     // primitive types (bits 0-7) and PRIM flags (bits 8-15) of what it drew
};
struct ValueStat {
    uint64_t count = 0;
    uint16_t site = 0;
};

CRITICAL_SECTION g_lock;  // the statistics: kicks (game threads, interrupt context) against the VBlank
bool g_active = false, g_measuring = false;
std::unordered_map<uint16_t, SiteStats> g_stats;
uint64_t g_cat_bytes[kCats];
std::map<std::pair<uint32_t, uint64_t>, ValueStat> g_values;  // (path << 8 | reg, value)
std::map<std::pair<int, uint16_t>, uint64_t> g_kick_bytes;     // (channel, kicker)
uint64_t g_batches = 0, g_kicks_learn = 0, g_kicks_measure = 0;
uint16_t g_kicker[3], g_tag_site[3];

uint16_t g_last_site = 0;
SiteStats *g_last_stats = nullptr;  // the last site's entry (unordered_map entries stay put)
SiteStats &stats(uint16_t site) {
    if (site != g_last_site || !g_last_stats) {
        g_last_site = site;
        g_last_stats = &g_stats[site];
    }
    return *g_last_stats;
}

struct W {
    uint32_t value;
    uintptr_t addr;
    uint16_t site;
};

uint16_t pseudo(uint32_t kind, uintptr_t addr, uint16_t tag_site) {
    const uint32_t page = static_cast<uint32_t>(addr & ~uintptr_t(0xFFF));
    const uint32_t via = site_real(tag_site) ? site_writer(tag_site) : 0;
    return site_id(kind == kElsewhere ? 0 : page, via, kind);
}

uint16_t lookup(uintptr_t a, uint16_t tag_site, bool by_ref) {
    uint32_t word;
    int view;
    uintptr_t end;
    if (!locate(a, word, view, end)) return pseudo(kElsewhere, a, tag_site);
    if (const uint16_t s = g_shadow[word]) return s;
    const uint32_t page = word >> 10;
    if (!g_win[page]) {
        if (by_ref && site_real(tag_site)) return site_id(site_writer(tag_site), site_caller(tag_site), kRefBy);
        return pseudo(kUnwatched, a, tag_site);
    }
    if (a >= kElfStart && a + 4 <= kElfEnd && g_elf_copy &&
        std::memcmp(g_elf_copy + (a - kElfStart), reinterpret_cast<const void *>(a), 4) == 0)
        return pseudo(kStatic, a, tag_site);
    return pseudo(kStale, a, tag_site);
}

void emit(int cat, const W &w) {
    if (cat_watched(cat)) {
        uint32_t word;
        int view;
        uintptr_t end;
        if (locate(w.addr, word, view, end)) g_want[word >> 10] = 1;
    }
    if (!g_measuring) return;
    g_cat_bytes[cat] += 4;
    stats(w.site).bytes[cat] += 4;
}

// The pgl state flush (004AB350 -> 004AB318) packs the shadow registers of the context at 0x70002000: shared
// ones at +0x120 (register codes in D_0062A040), context 1 and 2 at +0x170 (codes D_0062A050 / D_0062A060, one
// storage). A value it packed is attributed to whoever last wrote its shadow slot: the state setter.
constexpr uint32_t kPacker = 0x004AB318, kCtx = 0x70002000;
uint32_t shadow_slot(uint32_t reg, uint64_t v) {
    uint32_t at = 0;
    for (uint32_t k = 0; k < 10 && !at; ++k)
        if (guest_ref<uint8_t>(0x0062A040 + k) == reg) at = kCtx + 0x120 + 8 * k;
    for (uint32_t k = 0; k < 13 && !at; ++k)
        if (guest_ref<uint8_t>(0x0062A050 + k) == reg || guest_ref<uint8_t>(0x0062A060 + k) == reg) at = kCtx + 0x170 + 8 * k;
    return at && guest_ref<uint64_t>(at) == v ? at : 0;
}
std::map<std::pair<uint16_t, uint32_t>, uint64_t> g_setters;  // (setter site, path << 8 | reg) -> values packed

void note_value(int path, uint32_t reg, uint64_t v, uint16_t site) {
    if (!g_measuring) return;
    if (site_real(site) && site_writer(site) == kPacker) {
        uint16_t setter = 0;
        if (const uint32_t at = shadow_slot(reg, v)) setter = g_shadow[(kRamSize + (at - kScratchpad)) >> 2];
        ++g_setters[{setter, static_cast<uint32_t>(path) << 8 | reg}];
    }
    SiteStats &s = stats(site);
    s.regs[reg >> 6 & 1] |= 1ull << (reg & 63);
    if (reg == 0x00 || reg == 0x1B) s.prims |= (1u << (v & 7)) | ((static_cast<uint32_t>(v) >> 3 & 0xFF) << 8);
    if (!value_kept(reg, v)) return;
    ValueStat &vs = g_values[{static_cast<uint32_t>(path) << 8 | reg, v}];
    if (!vs.count) vs.site = site;
    ++vs.count;
}

// GIF packets: path 2 (VIF1 DIRECT) and path 3 (the GIF channel), a qword at a time.
struct Gif {
    int path, base;
    int st = 0;  // 0 tag, 1 PACKED, 2 REGLIST, 3 IMAGE
    uint32_t nloop = 0, nreg = 0, loop = 0, r = 0, left = 0;
    uint64_t regs = 0;

    void qword(const W *q) {
        auto all = [&](int c) {
            for (int k = 0; k < 4; ++k) emit(base + c, q[k]);
        };
        const uint64_t lo = q[0].value | static_cast<uint64_t>(q[1].value) << 32;
        switch (st) {
        case 0: {
            all(gTag);
            const uint64_t hi = q[2].value | static_cast<uint64_t>(q[3].value) << 32;
            nloop = lo & 0x7FFF;
            const uint32_t flg = (lo >> 58) & 3;
            nreg = (lo >> 60) & 0xF;
            if (!nreg) nreg = 16;
            regs = hi;
            if ((lo >> 46) & 1) note_value(path, 0x00, (lo >> 47) & 0x7FF, q[1].site);
            if (g_measuring && flg < 2) {  // the registers the tag's descriptors name (A+D ones come with their data)
                SiteStats &s = stats(q[2].site);
                for (uint32_t k = 0; k < nreg; ++k) {
                    const uint32_t d = (hi >> (4 * k)) & 0xF;
                    if (d != 0xE && d != 0xF) s.regs[0] |= 1ull << d;  // descriptors 0-0xD are registers 0-0xD
                }
            }
            if (!nloop) return;
            loop = r = 0;
            if (flg == 0) st = 1;
            else if (flg == 1) st = 2, left = (nloop * nreg + 1) / 2;
            else st = 3, left = nloop;
            return;
        }
        case 1: {
            const uint32_t d = (regs >> (4 * r)) & 0xF;
            if (d == 0xE) {
                const uint32_t reg = q[2].value & 0xFF;
                all(ad_group(reg));
                note_value(path, reg, lo, q[0].site);
            } else {
                all(d == 0xF ? gAdOther : gPacked);
                if (d == 0) note_value(path, 0x00, lo & 0x7FF, q[0].site);
            }
            if (++r == nreg) {
                r = 0;
                if (++loop == nloop) st = 0;
            }
            return;
        }
        case 2:
            all(gReglist);
            for (int h = 0; h < 2; ++h) {  // two 64-bit entries; a PRIM one gives the primitive drawn
                if (loop >= nloop) break;
                if (((regs >> (4 * r)) & 0xF) == 0)
                    note_value(path, 0x00, q[2 * h].value & 0x7FF, q[2 * h].site);
                if (++r == nreg) r = 0, ++loop;
            }
            if (--left == 0) st = 0;
            return;
        default:
            all(gImage);
            if (--left == 0) st = 0;
            return;
        }
    }
};

Gif g_gif2{2, kGif2}, g_gif3{3, kGif3};

// VIF streams (VIF1; VIF0 is only counted), a word at a time.
struct Vif {
    int ch;
    int mode = 0;  // 0 code, 1 UNPACK, 2 MPG, 3 DIRECT, 4 STMASK/STROW/STCOL data
    uint32_t left = 0, cl = 1, wl = 1;
    uint32_t bits = 32, addr = 0, flg = 0, vn = 0, vl = 0, done = 0;
    W q[4];
    int qn = 0;

    void word(const W &w) {
        const int code_cat = ch == 0 ? kVif0 : kVifCode;
        if (mode == 0) {
            emit(code_cat, w);
            const uint32_t cmd = (w.value >> 24) & 0x7F, num = (w.value >> 16) & 0xFF, imm = w.value & 0xFFFF;
            if (cmd == 0x01) {
                cl = imm & 0xFF;
                wl = (imm >> 8) & 0xFF;
            } else if (cmd == 0x20) {
                mode = 4, left = 1;
            } else if (cmd == 0x30 || cmd == 0x31) {
                mode = 4, left = 4;
            } else if (cmd == 0x4A) {
                mode = 2, left = 2 * (num ? num : 256);
            } else if (cmd == 0x50 || cmd == 0x51) {
                mode = 3, left = 4 * (imm ? imm : 65536), qn = 0;
            } else if (cmd == 0x14 || cmd == 0x15 || cmd == 0x17) {
                if (g_measuring && ch == 1) ++g_batches;
            } else if (cmd >= 0x60) {
                vn = (cmd >> 2) & 3;
                vl = cmd & 3;
                const uint32_t count0 = num ? num : 256;
                const uint32_t count = wl <= cl ? count0 : cl * (count0 / wl) + std::min(count0 % wl, cl);
                bits = (32 >> vl) * (vn + 1);
                left = (count * bits + 31) / 32;
                addr = imm & 0x3FF;
                flg = (imm >> 15) & 1;
                done = 0;
                qn = 0;
                mode = left ? 1 : 0;
            }
            return;
        }
        switch (mode) {
        case 4: emit(code_cat, w); break;
        case 2: emit(ch == 0 ? kVif0 : kVifMpg, w); break;
        case 3:
            q[qn++] = w;
            if (qn == 4) {
                g_gif2.qword(q);
                qn = 0;
            }
            break;
        default: {
            const uint32_t elem = static_cast<uint32_t>((static_cast<uint64_t>(done) * 32) / bits);
            const uint32_t c = cl ? cl : 1, l = wl ? wl : 1;
            uint32_t dest = l <= c ? addr + (elem / l) * c + elem % l : addr + (elem / c) * l + elem % c;
            dest &= 0x3FF;
            const uint32_t rel = flg ? dest : (dest >= 435 ? dest - 435 : dest);
            const int cat = ch == 0 ? kVif0 : rel < 192 ? kUnpVertex : rel == 192 ? kUnpGiftag : rel < 217 ? kUnpConst
                                            : rel < 242 ? kUnpState : kUnpOther;
            if (cat == kUnpState && vn == 3 && vl == 0) {  // the batch's GS state packet: tag at 217, then A+D
                q[qn++] = w;
                if (qn == 4) {
                    for (int k = 0; k < 4; ++k) emit(kUnpState, q[k]);
                    if (rel != 217) note_value(1, q[2].value & 0xFF, q[0].value | static_cast<uint64_t>(q[1].value) << 32, q[0].site);
                    qn = 0;
                }
            } else {
                emit(cat, w);
            }
            ++done;
        }
        }
        if (--left == 0) mode = 0;
    }
};

Vif g_vif[2] = {{0}, {1}};

// ---------------------------------------------------------------------------------------------
// Windows.

struct Window {
    uint32_t from, count;
};
std::vector<Window> g_windows;
size_t g_next = 0;
enum Phase { kOff, kLearn, kWarm, kMeasure } g_phase = kOff;
uint32_t g_phase_at = 0, g_measured_vblanks = 0;
std::string g_dir = ".";

std::unordered_map<uint32_t, const char *> &names() {
    static std::unordered_map<uint32_t, const char *> m;
    if (m.empty())
        for (uint32_t i = 0; i < gt4_native_function_count; ++i) m[gt4_native_functions[i].addr] = gt4_native_functions[i].name;
    return m;
}
const char *name_of(uint32_t a) {
    auto it = names().find(a);
    return it == names().end() ? "-" : it->second;
}

void report(uint32_t from) {
    char path[512];
    snprintf(path, sizeof path, "%s/gs_writers_%u.txt", g_dir.c_str(), from);
    FILE *f = fopen(path, "w");
    if (!f) {
        log_warn("gs_writers: cannot write %s", path);
        return;
    }
    uint32_t pages = 0;
    for (uint32_t p = 0; p < kPages; ++p) pages += g_prot[p];
    fprintf(f, "# GT4_GS_WRITERS report: what the DMA channels delivered towards the GS, by the game function that wrote\n"
               "# each 32-bit word. Lines are tab-separated; bytes are totals over the window (divide by frames).\n");
    const uint32_t frames = g_windows[g_next].count;
    fprintf(f, "window\tfrom=%u\tframes=%u\tvblanks=%u\tfaults=%ld\tlost=%ld\tpages=%u\tfailed_pages=%ld\tbatches=%llu\tkicks_learn=%llu\tkicks_measure=%llu\n",
            from, frames, g_measured_vblanks, g_faults, g_lost, pages, g_failed_pages, static_cast<unsigned long long>(g_batches),
            static_cast<unsigned long long>(g_kicks_learn), static_cast<unsigned long long>(g_kicks_measure));
    for (int c = 0; c < kCats; ++c)
        if (g_cat_bytes[c]) fprintf(f, "cat\t%s\t%llu\n", cat_name(c).c_str(), static_cast<unsigned long long>(g_cat_bytes[c]));
    std::vector<std::pair<uint64_t, uint16_t>> order;
    for (auto &[site, s] : g_stats) {
        uint64_t total = 0;
        for (int c = 0; c < kCats; ++c) total += s.bytes[c];
        order.push_back({total, site});
    }
    std::sort(order.rbegin(), order.rend());
    for (auto &[total, site] : order) {
        const SiteStats &s = g_stats[site];
        const uint32_t w = site_writer(site), k = site_kind(site), cr = site_caller(site);
        const char *wn = k <= kRefBy ? name_of(w) : "-";
        for (int c = 0; c < kCats; ++c)
            if (s.bytes[c])
                fprintf(f, "site\t%08x\t%s\t%08x\t%s\t%s\t%s\t%llu\n", w, wn, cr, cr ? name_of(cr) : "-", kKindNames[k],
                        cat_name(c).c_str(), static_cast<unsigned long long>(s.bytes[c]));
        if (s.regs[0] | s.regs[1]) {
            fprintf(f, "regs\t%08x\t%08x\t%s\t", w, cr, kKindNames[k]);
            for (uint32_t r = 0; r < 128; ++r)
                if ((s.regs[r >> 6] >> (r & 63)) & 1) fprintf(f, "%s ", reg_name(r));
            fprintf(f, "\n");
        }
        if (s.prims) fprintf(f, "prims\t%08x\t%08x\t%s\t%04x\n", w, cr, kKindNames[k], s.prims);
    }
    for (auto &[key, bytes] : g_kick_bytes) {
        const uint32_t w = site_writer(key.second), cr = site_caller(key.second);
        fprintf(f, "kick\t%d\t%08x\t%s\t%08x\t%s\t%llu\n", key.first, w, name_of(w), cr, cr ? name_of(cr) : "-",
                static_cast<unsigned long long>(bytes));
    }
    for (auto &[key, vs] : g_values) {
        const uint32_t w = site_writer(vs.site);
        fprintf(f, "value\t%u\t%s\t%016llx\t%llu\t%08x\t%s\t%s\n", key.first >> 8, reg_name(key.first & 0xFF),
                static_cast<unsigned long long>(key.second), static_cast<unsigned long long>(vs.count), w,
                site_kind(vs.site) <= kRefBy ? name_of(w) : "-", kKindNames[site_kind(vs.site)]);
    }
    for (auto &[key, count] : g_setters) {
        const uint16_t s = key.first;
        const uint32_t w = s ? site_writer(s) : 0, cr = s ? site_caller(s) : 0, k = s ? site_kind(s) : kStale;
        fprintf(f, "setter\t%08x\t%s\t%08x\t%s\t%s\t%u\t%s\t%llu\n", w, k <= kRefBy ? name_of(w) : "-", cr,
                cr ? name_of(cr) : "-", s ? kKindNames[k] : "unknown", key.second >> 8, reg_name(key.second & 0xFF),
                static_cast<unsigned long long>(count));
    }
    fclose(f);
    // a summary in the log: the categories and the ten largest writers of packet bytes (pixels left out)
    log_info("gs_writers: window from VBlank %u, %u frames measured over %u VBlanks, %ld faults, %u pages watched: %s",
             from, frames, g_measured_vblanks, g_faults, pages, path);
    for (int c = 0; c < kCats; ++c)
        if (g_cat_bytes[c])
            log_info("gs_writers:   %-20s %10llu bytes/frame", cat_name(c).c_str(),
                     static_cast<unsigned long long>(g_cat_bytes[c] / std::max(1u, frames)));
}

void reset_stats() {
    g_stats.clear();
    g_last_stats = nullptr;
    std::memset(g_cat_bytes, 0, sizeof g_cat_bytes);
    g_values.clear();
    g_setters.clear();
    g_kick_bytes.clear();
    g_batches = 0;
    g_kicks_measure = 0;
    g_faults = g_lost = 0;
}

// The RAM views must stay one memory: a page that differs between the main view and a mirror says the
// protection changes broke the sharing.
void check_mirrors(const char *when) {
    int differ = 0, first = -1;
    for (uint32_t p = 0; p < kRamPages; ++p) {
        if (!g_ever[p]) continue;
        const void *main = reinterpret_cast<const void *>(view_addr(0, p));
        if (std::memcmp(main, reinterpret_cast<const void *>(kUncachedMirror + p * 4096u), 4096) ||
            std::memcmp(main, reinterpret_cast<const void *>(kUncachedAccelMirror + p * 4096u), 4096)) {
            if (first < 0) first = static_cast<int>(p);
            ++differ;
        }
    }
    if (differ) log_warn("gs_writers: %s: %d watched pages differ between RAM and its mirrors (first %08x)", when, differ, first * 4096);
}

struct Lock {
    Lock() { EnterCriticalSection(&g_lock); }
    ~Lock() { LeaveCriticalSection(&g_lock); }
};

}  // namespace

void init() {
    const char *e = getenv("GT4_GS_WRITERS");
    if (!e || !*e) return;
    for (const char *p = e; *p;) {
        unsigned from = 0, count = 0;
        int n = 0;
        if (sscanf(p, "%u,%u%n", &from, &count, &n) < 2) break;
        g_windows.push_back({from, count});
        p += n;
        if (*p == ';' || *p == ' ') ++p;
    }
    if (g_windows.empty()) {
        log_warn("gs_writers: GT4_GS_WRITERS=%s: expected FROM,COUNT[;FROM,COUNT...]", e);
        return;
    }
    std::sort(g_windows.begin(), g_windows.end(), [](const Window &a, const Window &b) { return a.from < b.from; });
    if (const char *d = getenv("GT4_GS_WRITERS_DIR"); d && *d) g_dir = d;
    g_watch_image = getenv("GT4_GS_WRITERS_IMAGE") != nullptr;
    InitializeCriticalSection(&g_lock);
    g_sites = static_cast<volatile LONG64 *>(VirtualAlloc(nullptr, kSiteSlots * 8, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    g_shadow = static_cast<uint16_t *>(VirtualAlloc(nullptr, kPages * 1024 * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    g_elf_copy = static_cast<uint8_t *>(VirtualAlloc(nullptr, kElfEnd - kElfStart, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!g_sites || !g_shadow || !g_elf_copy) {
        log_warn("gs_writers: out of memory");
        return;
    }
    std::memcpy(g_elf_copy, guest_ptr(kElfStart), kElfEnd - kElfStart);
    g_text_end = reinterpret_cast<uintptr_t>(&gt4_game_text_end);
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(GetModuleHandleW(nullptr));
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(reinterpret_cast<const uint8_t *>(dos) + dos->e_lfanew);
    g_exe_lo = reinterpret_cast<uintptr_t>(dos);
    g_exe_hi = g_exe_lo + nt->OptionalHeader.SizeOfImage;
    AddVectoredExceptionHandler(1, on_write);
    g_on = true;
    log_info("gs_writers: %zu window(s), the first from VBlank %u for %u frames; reports in %s", g_windows.size(),
             g_windows[0].from, g_windows[0].count, g_dir.c_str());
}

bool active() { return g_active; }

namespace {

void protect_wanted() {
    for (uint32_t p = 0; p < kPages; ++p)
        if (g_want[p] && !g_prot[p] && !protect(p)) {
            g_want[p] = 0;
            InterlockedIncrement(&g_failed_pages);
        }
}

// A window opens at a VBlank; its phases go by frames: the frame's VIF1 chain kicked by 004A11F8 (the swap,
// 00107878 / 001079C8). Two frames learn the pages (nothing protected), two warm the shadow, then COUNT frames
// are measured, each from its kick to the next.
uint32_t g_vblank = 0, g_phase_vblank = 0;

void frame_kicked() {
    ++g_phase_at;
    switch (g_phase) {
    case kLearn:
        if (g_phase_at >= 2) {
            g_want[(kRamSize + (kCtx - kScratchpad)) >> 12] = 1;  // the pgl context: the state setters (shadow_slot)
            InterlockedExchange(&g_watching, 1);
            g_phase = kWarm;
            g_phase_at = 0;
        }
        break;
    case kWarm:
        if (g_phase_at >= 2) {
            reset_stats();
            g_measuring = true;
            g_phase = kMeasure;
            g_phase_at = 0;
            g_phase_vblank = g_vblank;
        }
        break;
    case kMeasure:
        if (g_phase_at >= g_windows[g_next].count) {
            g_measuring = false;
            g_measured_vblanks = g_vblank - g_phase_vblank;
            report(g_windows[g_next].from);
            InterlockedExchange(&g_watching, 0);
            for (uint32_t p = 0; p < kPages; ++p)
                if (g_ever[p]) unprotect(p);
            check_mirrors("window end");
            g_phase = kOff;
            g_active = false;
            ++g_next;
            log_warn("gs_writers: the watched pages are now private to RAM's main view (the mirrors no longer see "
                     "them): the rest of this run is not reliable; measure one window per run");
        }
        break;
    default:
        break;
    }
    if (g_watching) protect_wanted();
}

}  // namespace

void vblank(uint32_t v) {
    Lock lock;
    g_vblank = v;
    if (g_phase == kOff && g_next < g_windows.size() && v >= g_windows[g_next].from) {
        std::memset(g_want, 0, sizeof g_want);
        std::memset(g_win, 0, sizeof g_win);
        std::memset(g_shadow, 0, kPages * 1024 * 2);
        g_kicks_learn = 0;
        g_active = true;
        g_phase = kLearn;
        g_phase_at = 0;
    }
    if (g_watching) protect_wanted();  // pages first seen in this frame's packets
}

void kick(int channel, const void *ret, const void *frame) {
    if (!g_active || channel < 0 || channel > 2) return;
    Lock lock;
    uint32_t writer = 0x004AB6D8, caller = 0;  // no return address: the DMA handler (004AB6D8) restarted it
    if (ret) {
        uint32_t g = 0;
        writer = game_pc(reinterpret_cast<uintptr_t>(ret), g) ? g : 0;
        const NT_TIB *tib = reinterpret_cast<const NT_TIB *>(NtCurrentTeb());
        const uintptr_t lo = reinterpret_cast<uintptr_t>(tib->StackLimit), hi = reinterpret_cast<uintptr_t>(tib->StackBase);
        // frame: the replacement's frame; [frame] is the kicking function's frame, [that + 4] its return address
        const uintptr_t fp = reinterpret_cast<uintptr_t>(frame);
        if (fp >= lo && fp + 4 <= hi) {
            const uintptr_t up = *reinterpret_cast<const uint32_t *>(fp);
            if (up >= lo && up + 8 <= hi) game_pc(reinterpret_cast<const uint32_t *>(up)[1], caller);
        }
    }
    g_kicker[channel] = site_id(writer, caller, kDirect);
    if (g_measuring) ++g_kicks_measure;
    else ++g_kicks_learn;
    if (channel == 1 && writer == 0x004A11F8) frame_kicked();
}

void tag(int channel, const uint8_t *t) {
    if (!g_active || channel < 0 || channel > 2) return;
    Lock lock;
    const uintptr_t a = reinterpret_cast<uintptr_t>(t);
    const uint16_t s0 = lookup(a, 0, false);
    g_tag_site[channel] = s0;
    emit(kDmaTag, W{*reinterpret_cast<const uint32_t *>(t), a, s0});
    emit(kDmaTag, W{*reinterpret_cast<const uint32_t *>(t + 4), a + 4, lookup(a + 4, 0, false)});
}

void data(int channel, const uint8_t *p, uint32_t bytes, bool by_ref) {
    if (!g_active || channel < 0 || channel > 2) return;
    LARGE_INTEGER t0, t1;
    QueryPerformanceCounter(&t0);
    {
        Lock lock;
        const uint16_t tag_site = g_tag_site[channel];
        for (uint32_t k = 0; k + 4 <= bytes; k += 4) {
            const uintptr_t a = reinterpret_cast<uintptr_t>(p + k);
            const W w{*reinterpret_cast<const uint32_t *>(a), a, lookup(a, tag_site, by_ref)};
            if (channel == 2) {
                static W q[4];
                static int qn = 0;
                q[qn++] = w;
                if (qn == 4) {
                    g_gif3.qword(q);
                    qn = 0;
                }
            } else {
                g_vif[channel].word(w);
            }
        }
        if (g_measuring) g_kick_bytes[{channel, g_kicker[channel]}] += bytes;
    }
    if (g_watching) {
        QueryPerformanceCounter(&t1);
        kernel_add_stall(t1.QuadPart - t0.QuadPart);
    }
}

}  // namespace gt4::rt::gsw
