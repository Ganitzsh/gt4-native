// GT4_DIFFTEST=NAME[,NAME...] (a gt4_game linked with `game_link.py --eeorig`): runs the native code and the original
// code of game functions on the same random inputs and compares everything they write; logs a line per test and
// exits (code 1 if any differs). For native rewrites of functions no recorded scene reaches (docs/NATIVE_ONLY.md):
// each test below knows its function's arguments and the memory it reads and writes. The buffers live in a scratch
// area of guest RAM the game has not used yet (the test runs before the game starts).
#include "guest.h"
#include "image.h"
#include "log.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" std::uint64_t gt4_orig_run_addr(const std::uint32_t *args, std::uint32_t addr);

namespace {

constexpr std::uint32_t kScratch = 0x01800000, kScratchSize = 0x00200000;

std::uint64_t g_rng = 0x2545F4914F6CDD1Dull;
std::uint32_t rnd() {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return static_cast<std::uint32_t>(g_rng >> 16);
}
std::uint8_t *ram(std::uint32_t a) { return static_cast<std::uint8_t *>(gt4::rt::guest_ptr(a)); }

void *native_fn(std::uint32_t addr) {
    for (std::uint32_t k = 0; k < gt4_native_function_count; ++k)
        if (gt4_native_functions[k].addr == addr) return gt4_native_functions[k].fn;
    return nullptr;
}

using Fn8 = std::uint32_t (*)(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
                              std::uint32_t, std::uint32_t);
struct Ret16 {
    std::uint32_t w[4];
};
// gcc 2.9's i386 code returns a 16-byte structure through a hidden first argument it pops itself
typedef Ret16(__attribute__((callee_pop_aggregate_return(1))) * Fn8Ret16)(std::uint32_t, std::uint32_t, std::uint32_t,
                                                                         std::uint32_t, std::uint32_t, std::uint32_t,
                                                                         std::uint32_t, std::uint32_t);

// One trial: `setup` fills the scratch area (the same bytes for both runs) and gives the arguments; both versions run
// from that state and the scratch area is compared after each.
struct Test {
    const char *name;
    std::uint32_t addr;
    bool ret16;   // the function returns a 16-byte structure (native: hidden pointer); the return is not compared
    void (*setup)(std::uint32_t args[8]);
};

void fill(std::uint32_t at, std::uint32_t bytes) {
    for (std::uint32_t k = 0; k < bytes; ++k) ram(at)[k] = static_cast<std::uint8_t>(rnd());
}

// image byte operations of the movie player (001C1850 ...): (dst, src[, src2], width, height[, value])
void image_mask(std::uint32_t a[8]) {
    a[0] = kScratch;
    a[1] = kScratch + 0x40000;
    a[2] = 1 + rnd() % 80;   // width
    a[3] = 1 + rnd() % 12;   // height
    a[4] = rnd();            // value (its low byte)
    fill(kScratch, 0x80000);
}
void image_two(std::uint32_t a[8]) {
    a[0] = kScratch;
    a[1] = kScratch + 0x40000;
    a[2] = kScratch + 0x60000;
    a[3] = 1 + rnd() % 80;
    a[4] = 1 + rnd() % 12;
    fill(kScratch, 0x80000);
}
void image_one(std::uint32_t a[8]) {
    a[0] = kScratch;
    a[1] = kScratch + 0x40000;
    a[2] = 1 + rnd() % 80;
    a[3] = 1 + rnd() % 12;
    fill(kScratch, 0x80000);
}

const Test kTests[] = {
    {"001C1850", 0x001C1850, true, image_mask},
    {"001C18B0", 0x001C18B0, false, image_mask},
    {"001C1C50", 0x001C1C50, false, image_two},
    {"001C1CA8", 0x001C1CA8, false, image_one},
};

bool run(const Test &t, int trials) {
    void *fn = native_fn(t.addr);
    if (!fn) {
        gt4::rt::log_error("difftest %s: no native function", t.name);
        return false;
    }
    std::vector<std::uint8_t> before(kScratchSize), native(kScratchSize);
    int differ = 0;
    for (int n = 0; n < trials; ++n) {
        std::uint32_t args[8] = {};
        t.setup(args);
        std::memcpy(before.data(), ram(kScratch), kScratchSize);
        if (t.ret16)
            reinterpret_cast<Fn8Ret16>(fn)(args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]);
        else
            reinterpret_cast<Fn8>(fn)(args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]);
        std::memcpy(native.data(), ram(kScratch), kScratchSize);
        std::memcpy(ram(kScratch), before.data(), kScratchSize);
        gt4_orig_run_addr(args, t.addr);
        if (std::memcmp(native.data(), ram(kScratch), kScratchSize) != 0) {
            std::uint32_t first = 0;
            while (native[first] == ram(kScratch)[first]) ++first;
            if (++differ <= 3)
                gt4::rt::log_warn("difftest %s trial %d (args %x %x %x %x %x): first difference at %08x: native %02x original %02x",
                                  t.name, n, args[0], args[1], args[2], args[3], args[4], kScratch + first, native[first],
                                  ram(kScratch)[first]);
        }
    }
    gt4::rt::log_info("difftest %s: %d trials, %d differ", t.name, trials, differ);
    return differ == 0;
}

}  // namespace

// From gt4_orig_install: runs the tests GT4_DIFFTEST names ("all" for every one) and exits.
extern "C" void gt4_difftest(const char *names) {
    const int trials = getenv("GT4_DIFFTEST_TRIALS") ? atoi(getenv("GT4_DIFFTEST_TRIALS")) : 200;
    const std::string list = std::string(",") + names + ",";
    bool ok = true;
    for (const Test &t : kTests)
        if (list == ",all," || list.find(std::string(",") + t.name + ",") != std::string::npos) ok = run(t, trials) && ok;
    std::exit(ok ? 0 : 1);
}
