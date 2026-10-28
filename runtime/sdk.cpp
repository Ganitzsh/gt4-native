// Replacements of Sony SDK functions linked in the image (config/sdk_functions.tsv in gt4-decomp):
// a function named gt4_replace_<SDK name> takes over that function's address (tools/game_link.py).
#include "kernel.h"
#include "log.h"
#include "runtime.h"
#include "ps2float/ps2float.h"

#include <cstdarg>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>

using namespace gt4::rt;

// diei.o: DIntr/EIntr disable/enable interrupts (di/ei on the EE) and return the previous state.
extern "C" int gt4_replace_DIntr() { return kernel_di(); }
extern "C" int gt4_replace_func_005B72F8() { return kernel_ei(); }  // EIntr (diei.o, after DIntr)

// gcc 2.x's builtin that the game's exception runtime (frame registration, init_reg_size_table)
// calls: fills the DWARF register size table. The EE's 32 GPRs and 32 FPRs; unwinding through the
// original MIPS tables is not used natively.
extern "C" void gt4_init_dwarf_reg_size_table(unsigned char *table) __asm__("___builtin_init_dwarf_reg_size_table");
extern "C" void gt4_init_dwarf_reg_size_table(unsigned char *table) {
    for (int i = 0; i < 32; ++i) table[i] = 8;
    for (int i = 32; i < 66; ++i) table[i] = 4;
}

// thread.o's StartThread wrapper (005AEDE0) around the syscall.
extern "C" int gt4_sys_StartThread(int id, uint32_t arg);
extern "C" int gt4_replace_StartThread(int id, uint32_t arg) { return gt4_sys_StartThread(id, arg); }

// Debugging aid: how many x87 registers are in use (8 - the empty ones in the tag word). Game code
// must leave the x87 stack empty between functions; a float result called as void leaks one.
extern "C" int gt4_fpu_depth() {
    unsigned short env[14];
    __asm__ volatile("fnstenv %0; fldenv %0" : "=m"(env));
    int used = 0;
    for (int r = 0; r < 8; ++r) used += ((env[4] >> (2 * r)) & 3) != 3;
    return used;
}

// glue.o write (005AE268): newlib's console output. On the PS2 descriptors 1 and 2 go to the
// debug console (the game's printf: the script VM's error reports among them) and any other
// descriptor fails; here the console is the run log, one log line per text line.
extern "C" int gt4_replace_write(int fd, const char *buf, int n) {
    if (fd != 1 && fd != 2) return -1;
    static char line[1024];
    static int used;
    for (int i = 0; i < n; ++i) {
        char c = buf[i];
        if (c == '\n' || used == (int)sizeof line - 1) {
            line[used] = 0;
            gt4::rt::log_info("game: %s", line);
            used = 0;
            if (c == '\n') continue;
        }
        line[used++] = c;
    }
    return n;
}

// Debugging aid for game-side code (game_src checks): printf-style text into the run log.
extern "C" void gt4_logf(const char *fmt, ...) {
    char text[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    gt4::rt::log_info("%s", text);
}

// The display shape for game_src/func_004365B8.cpp (the game's wide mode): --aspect W:H or a
// number, x 1000; 0 when not given (the game's own option is kept).
extern "C" int gt4_display_aspect_milli() {
    const char *a = gt4::rt::g_config.aspect;
    if (!a || !*a) return 0;
    double w = 0, h = 1;
    if (std::sscanf(a, "%lf:%lf", &w, &h) < 1 || w <= 0 || h <= 0) return 0;
    return static_cast<int>(w / h * 1000.0 + 0.5);
}

// For game_src/func_00214FB0.cpp: 1 when the menus' 2D keeps a centred 4:3 area in the wide mode
// (--menus centered, the default), 0 when it is stretched as on the PS2 (--menus stretched).
extern "C" int gt4_menus_centered() {
    const char *m = gt4::rt::g_config.menus;
    return !(m && std::strcmp(m, "stretched") == 0);
}

// For the native 2D emitters (game_src/func_0049A200.cpp, 0049A360, 0049A4F8, 0049A6A0): with the menus in a
// centred 4:3 area of the wide mode (game_src/func_00214FB0.cpp), a primitive that spans that whole area (the
// layout's full width: backgrounds, fades) is widened to the frame's edges, as the PS2 stretches it in its own
// 16:9 mode. xyz points at the first vertex's XYZF2 (x in the low 16 bits, 1/16 pixel, offset 2048 at the
// frame's centre as GT4 sets XYOFFSET), the vertices `stride` bytes apart. Otherwise the sides keep stale
// pixels (the dealers' backdrop feeds its frame back into itself).
extern "C" void gt4_menu_band_widen(char *xyz, int stride, int n) {
    const int aspect = gt4_display_aspect_milli();
    if (n <= 0 || aspect < 1500 || !gt4_menus_centered() || !*reinterpret_cast<volatile int *>(0x006184F0)) return;
    int lo = 0xFFFF, hi = 0;
    for (int i = 0; i < n; ++i) {
        const int x = *reinterpret_cast<uint16_t *>(xyz + i * stride);
        lo = x < lo ? x : lo;
        hi = x > hi ? x : hi;
    }
    const double band = 640.0 * 16.0 * (4.0 / 3.0) / (aspect / 1000.0);   // the centred area's width, 1/16 px
    const double centre = 2048.0 * 16.0;
    if (std::fabs((lo + hi) * 0.5 - centre) > 32.0 || std::fabs((hi - lo) - band) > 48.0) return;
    const double scale = 640.0 * 16.0 / band;
    for (int i = 0; i < n; ++i) {
        uint16_t *x = reinterpret_cast<uint16_t *>(xyz + i * stride);
        const double wide = centre + (*x - centre) * scale;
        *x = static_cast<uint16_t>(wide < 0.0 ? 0.0 : wide > 65535.0 ? 65535.0 : wide + 0.5);
    }
}

// For game_src/func_0043AF68.cpp (testing): GT4_MONEY=N, the least money the player has; -1 when not set.
extern "C" long long gt4_money_floor() {
    static const long long floor = [] {
        const char *e = std::getenv("GT4_MONEY");
        return e && *e ? std::strtoll(e, nullptr, 10) : -1ll;
    }();
    return floor;
}

// The host's gameplay options (gameplay.* of its settings), sent at start and whenever they change
// (GT4_REC_HOST_CMD 4). 0: gameplay.ai_catchup (game_src/func_00365790.cpp), 0 original, 1 reduced, 2 off;
// GT4_AI_CATCHUP=original|reduced|off fixes it (headless tests).
namespace {
volatile int g_game_options[gt4::rt::kGameOptionCount];
int env_catchup() {
    static const int value = [] {
        const char *e = std::getenv("GT4_AI_CATCHUP");
        if (!e || !*e) return -1;
        if (!std::strcmp(e, "original") || !std::strcmp(e, "0")) return 0;
        if (!std::strcmp(e, "reduced") || !std::strcmp(e, "1")) return 1;
        if (!std::strcmp(e, "off") || !std::strcmp(e, "2")) return 2;
        gt4::rt::log_warn("GT4_AI_CATCHUP=%s: expected original, reduced or off", e);
        return -1;
    }();
    return value;
}
}  // namespace

void gt4::rt::set_game_option(uint32_t id, uint32_t value) {
    if (id >= kGameOptionCount) return;
    if (g_game_options[id] != static_cast<int>(value))
        log_info("game option %u = %u", id, value);
    g_game_options[id] = static_cast<int>(value);
}

extern "C" int gt4_game_option(int id) {
    if (id == gt4::rt::kOptionAiCatchUp && env_catchup() >= 0) return env_catchup();
    return id >= 0 && id < gt4::rt::kGameOptionCount ? g_game_options[id] : 0;
}

// The EE FPU's own arithmetic, bit for bit (runtime/ps2float: the PS2 float model of libretro/parallel-ps2,
// LGPL-3.0+): for game code whose results depend on the last bit (keystreams, checksums over float state), where the
// host's IEEE operations or plain truncation diverge. Raw single-precision bit patterns in and out.
extern "C" uint32_t gt4_ee_mul(uint32_t a, uint32_t b) { return ps2f_raw(ps2f_mul(a, b)); }
extern "C" uint32_t gt4_ee_div(uint32_t a, uint32_t b) { return ps2f_raw(ps2f_div(a, b)); }
extern "C" uint32_t gt4_ee_add(uint32_t a, uint32_t b) { return ps2f_raw(ps2f_add(a, b)); }
extern "C" uint32_t gt4_ee_sub(uint32_t a, uint32_t b) { return ps2f_raw(ps2f_sub(a, b)); }

// The VBlank counter, for game-side debugging checks (game_src copies).
extern "C" unsigned gt4_vblank() { return gt4::rt::kernel_vblank_count(); }
