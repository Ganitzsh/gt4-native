// GT4_DIFFTEST=NAME[,NAME...]|all (a gt4_game linked with `game_link.py --eeorig`): runs the native code and the
// original code of game functions on the same random inputs and compares everything they write and return; logs a
// line per test and exits (code 1 if any differs). For native rewrites of functions no recorded scene reaches
// (docs/NATIVE_ONLY.md): each test below knows its function's arguments, the memory it reads and writes, and the
// shared vectors it reads. The buffers live in a scratch area of guest RAM the game has not used
// yet (the test runs before the game starts).
#include "difftest.h"

#include "guest.h"
#include "image.h"
#include "log.h"
#include "shared_state.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" std::uint64_t gt4_orig_run_kinds(const std::uint32_t *args, std::uint32_t addr, const char *kinds, char ret);

namespace difftest {

std::uint64_t g_rng = 0x2545F4914F6CDD1Dull;
std::uint32_t rnd() {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return static_cast<std::uint32_t>(g_rng >> 16);
}
float rndf(float lo, float hi) { return lo + (hi - lo) * static_cast<float>(rnd() % 1000001) / 1000000.0f; }
std::uint32_t bits(float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}
std::uint8_t *ram(std::uint32_t a) { return static_cast<std::uint8_t *>(gt4::rt::guest_ptr(a)); }
float *ramf(std::uint32_t a) { return reinterpret_cast<float *>(ram(a)); }
void fill(std::uint32_t at, std::uint32_t bytes) {
    for (std::uint32_t k = 0; k < bytes; ++k) ram(at)[k] = static_cast<std::uint8_t>(rnd());
}
void fill_floats(std::uint32_t at, std::uint32_t count, float lo, float hi) {
    for (std::uint32_t k = 0; k < count; ++k) ramf(at)[k] = rndf(lo, hi);
}
void random_vectors(int first, int last, float lo, float hi) {
    for (int r = first; r <= last; ++r)
        for (int k = 0; k < 4; ++k) gt4_shared_vectors[r][k] = rndf(lo, hi);
}
float *vec(int n) { return gt4_shared_vectors[n]; }

std::vector<Test> &tests() {
    static std::vector<Test> all;
    return all;
}
void add(const Test &t) { tests().push_back(t); }

}  // namespace difftest

using namespace difftest;

namespace {

void *native_fn(std::uint32_t addr) {
    for (std::uint32_t k = 0; k < gt4_native_function_count; ++k)
        if (gt4_native_functions[k].addr == addr) return gt4_native_functions[k].fn;
    return nullptr;
}

using Fn8w = std::uint64_t (*)(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
                               std::uint32_t, std::uint32_t);
using Fn8 = std::uint32_t (*)(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
                              std::uint32_t, std::uint32_t);
struct Ret16 {
    std::uint32_t w[4];
};
// gcc 2.9's i386 code returns a 16-byte structure through a hidden first argument it pops itself
typedef Ret16(__attribute__((callee_pop_aggregate_return(1))) * Fn8Ret16)(std::uint32_t, std::uint32_t, std::uint32_t,
                                                                         std::uint32_t, std::uint32_t, std::uint32_t,
                                                                         std::uint32_t, std::uint32_t);


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

// 003E21D8 (out, in): in through rows 20..22. Unaligned three-float points.
void rotate_point(std::uint32_t a[8]) {
    fill(kScratch, 0x100);
    a[0] = kScratch + 0x10 + 4 * (rnd() % 4);
    a[1] = kScratch + 0x40 + 4 * (rnd() % 4);
    fill_floats(a[1], 3, -1000.0f, 1000.0f);
    random_vectors(20, 22, -2.0f, 2.0f);
}
// 003B7C98 (cam, view, screen, in, radius, near): a point through the current matrix, projected and tested.
void project_point(std::uint32_t a[8]) {
    fill(kScratch, 0x2400);
    a[0] = kScratch;   // the camera: m21B0, m21C4, m21D0, m21D4
    ramf(kScratch + 0x21B0)[0] = rndf(0.5f, 3.0f);
    ramf(kScratch + 0x21C4)[0] = rndf(0.5f, 3.0f);
    ramf(kScratch + 0x21D0)[0] = rndf(-0.5f, 0.5f);
    ramf(kScratch + 0x21D4)[0] = rndf(-0.5f, 0.5f);
    a[1] = kScratch + 0x2300;
    a[2] = kScratch + 0x2320;
    a[3] = kScratch + 0x2340 + 4 * (rnd() % 4);
    fill_floats(a[3], 3, -100.0f, 100.0f);
    a[4] = bits(rndf(0.0f, 20.0f));    // radius
    a[5] = bits(rndf(1.0f, 400.0f));   // near
    random_vectors(28, 31, -1.5f, 1.5f);
}

// a double's bits: mostly ordinary values, some zeros, infinities, NaNs, denormals and huge or tiny exponents
std::uint64_t random_double() {
    const std::uint32_t pick = rnd() % 16;
    const std::uint64_t sign = static_cast<std::uint64_t>(rnd() & 1) << 63;
    const std::uint64_t mant = (static_cast<std::uint64_t>(rnd()) << 32 | rnd()) & 0xFFFFFFFFFFFFFull;
    if (pick == 0) return sign;                                   // zero
    if (pick == 1) return sign | 0x7FF0000000000000ull;          // infinity
    if (pick == 2) return sign | 0x7FF0000000000000ull | (mant | 1);   // NaN
    if (pick == 3) return sign | mant;                            // denormal
    if (pick == 4) return sign | (static_cast<std::uint64_t>(1 + rnd() % 2046) << 52) | mant;   // any exponent
    return sign | (static_cast<std::uint64_t>(1023 - 40 + rnd() % 80) << 52) | mant;          // near 1
}
void one_double(std::uint32_t a[8]) {
    const std::uint64_t x = random_double();
    a[0] = static_cast<std::uint32_t>(x);
    a[1] = static_cast<std::uint32_t>(x >> 32);
}

const Register r0(Test{"001C1850", 0x001C1850, "iiiii", 's', false, image_mask});
const Register r1(Test{"001C18B0", 0x001C18B0, "iiiii", 'v', false, image_mask});
const Register r2(Test{"001C1C50", 0x001C1C50, "iiiii", 'v', false, image_two});
const Register r3(Test{"001C1CA8", 0x001C1CA8, "iiii", 'v', false, image_one});
const Register r4(Test{"003E21D8", 0x003E21D8, "ii", 'v', true, rotate_point});
const Register r5(Test{"003B7C98", 0x003B7C98, "iiiiff", 'i', true, project_point});
const Register r7(Test{"0057FB98", 0x0057FB98, "w", 'w', false, one_double});

// within float rounding (1e-5 relative), or within the drift of a long computation (1e-3 relative, 1e-5 absolute:
// the EE truncates where x87 rounds, and sums that cancel make the difference relative to the result large)
bool within(std::uint32_t a, std::uint32_t b, float rel, float abs) {
    float x, y;
    std::memcpy(&x, &a, 4);
    std::memcpy(&y, &b, 4);
    return std::fabs(x - y) <= rel * std::fmax(std::fabs(x), std::fabs(y)) + abs;
}
bool close(std::uint32_t a, std::uint32_t b) { return within(a, b, 1e-5f, 1e-30f); }
bool drift(std::uint32_t a, std::uint32_t b) { return within(a, b, 1e-3f, 1e-5f); }

// What a trial compares: the scratch area, the matrix stack (the original's VU0 data memory: the bridge hands it to
// the original code as such), the scratchpad and the test's own region of game data, as bytes (floats within
// tolerance when the test says so); then shared vectors 8..31, the matrix stack's top and the random state, the state
// the original carried between functions in VU0's registers (vectors 1..7 are temporaries a rewrite need not keep).
struct Region {
    const char *name;
    std::uint8_t *at;
    std::uint32_t addr, size;   // addr: the address reported
};

struct Snapshot {
    std::vector<std::uint8_t> mem[4];
    float vectors[32][4];
    std::uint16_t top;
    std::uint32_t random;
    void take(const Region *regions) {
        for (int r = 0; r < 4; ++r) mem[r].assign(regions[r].at, regions[r].at + regions[r].size);
        std::memcpy(vectors, gt4_shared_vectors, sizeof vectors);
        top = gt4_matrix_stack_top;
        random = gt4_random_state;
    }
    void put(const Region *regions) const {
        for (int r = 0; r < 4; ++r) std::memcpy(regions[r].at, mem[r].data(), regions[r].size);
        std::memcpy(gt4_shared_vectors, vectors, sizeof vectors);
        gt4_matrix_stack_top = top;
        gt4_random_state = random;
    }
};

bool run(const Test &t, int trials) {
    void *fn = native_fn(t.addr);
    if (!fn) {
        gt4::rt::log_error("difftest %s: no native function", t.name);
        return false;
    }
    const Region regions[4] = {
        {"scratch", ram(kScratch), kScratch, kScratchSize},
        {"matrix stack", reinterpret_cast<std::uint8_t *>(gt4_matrix_stack), 0, sizeof gt4_matrix_stack},
        {"scratchpad", ram(0x70000000), 0x70000000, 0x4000},
        {"data", t.data_size ? ram(t.data_addr) : nullptr, t.data_addr, t.data_size},
    };
    Snapshot before, native, original;
    int differ = 0, rounding = 0, drifted = 0;
    for (int n = 0; n < trials; ++n) {
        std::uint32_t args[8] = {};
        t.setup(args);
        before.take(regions);
        std::uint64_t native_ret = 0;
        if (t.ret == 'w')
            native_ret = reinterpret_cast<Fn8w>(fn)(args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]);
        else if (t.ret == 's')
            reinterpret_cast<Fn8Ret16>(fn)(args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]);
        else
            native_ret = reinterpret_cast<Fn8>(fn)(args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]);
        native.take(regions);
        before.put(regions);
        std::uint64_t orig_ret = gt4_orig_run_kinds(args, t.addr, t.kinds, t.ret == 's' ? 'v' : t.ret);
        original.take(regions);
        if (t.ret != 'w') {
            native_ret &= 0xFFFFFFFFu;
            orig_ret &= 0xFFFFFFFFu;
        }
        bool same = t.ret == 'v' || t.ret == 's' || native_ret == orig_ret ||
                    (t.ret == 'f' && t.floats && drift(static_cast<std::uint32_t>(native_ret), static_cast<std::uint32_t>(orig_ret)));
        const char *where = "";
        std::uint32_t at = 0, na = 0, oa = 0;
        auto word = [&](const char *region, std::uint32_t addr, std::uint32_t x, std::uint32_t y, bool exact = false) {
            if (x == y) return;
            if (t.floats && !exact && close(x, y)) {
                ++rounding;
                return;
            }
            if (t.floats && !exact && drift(x, y)) {
                ++drifted;
                return;
            }
            if (same) {
                where = region;
                at = addr;
                na = x;
                oa = y;
            }
            same = false;
        };
        for (int r = 0; r < 4; ++r)
            for (std::uint32_t k = 0; k < regions[r].size; k += 4) {
                std::uint32_t x, y;
                std::memcpy(&x, &native.mem[r][k], 4);
                std::memcpy(&y, &original.mem[r][k], 4);
                word(regions[r].name, regions[r].addr + k, x, y);
            }
        for (int v = 8; v < 32; ++v)
            for (int k = 0; k < 4; ++k)
                if (!(v == t.loose_vector && (t.loose_lanes & (1 << k))))
                    word("vector", static_cast<std::uint32_t>(v * 4 + k), bits(native.vectors[v][k]), bits(original.vectors[v][k]));
        word("matrix stack top", 0, native.top, original.top, true);
        word("random state", 0, native.random, original.random, true);
        if (!same && ++differ <= 3)
            gt4::rt::log_warn("difftest %s trial %d (args %08x %08x %08x %08x): return native %016llx original %016llx; first "
                              "differing word: %s %08x native %08x original %08x",
                              t.name, n, args[0], args[1], args[2], args[3], static_cast<unsigned long long>(native_ret),
                              static_cast<unsigned long long>(orig_ret), where, at, na, oa);
    }
    gt4::rt::log_info("difftest %s: %d trials, %d differ (%d words within float rounding, %d within 1e-3)", t.name, trials,
                      differ, rounding, drifted);
    return differ == 0;
}

}  // namespace

// From gt4_orig_install: runs the tests GT4_DIFFTEST names ("all" for every one) and exits.
extern "C" void gt4_difftest(const char *names) {
    const int trials = getenv("GT4_DIFFTEST_TRIALS") ? atoi(getenv("GT4_DIFFTEST_TRIALS")) : 200;
    const std::string list = std::string(",") + names + ",";
    bool ok = true;
    for (const Test &t : tests())
        if (list == ",all," || list.find(std::string(",") + t.name + ",") != std::string::npos) ok = run(t, trials) && ok;
    std::exit(ok ? 0 : 1);
}
