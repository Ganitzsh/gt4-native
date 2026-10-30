// Differential tests of batch G's native rewrites, which removed the last non-FPU inline asm (pref, sync, cache, the
// interrupt and $gp instructions, MMI); see difftest.h.
//
// Not tested here:
// - 0057F578 is dpadd, whose runtime replacement (runtime/native/unit_0057A128.cpp) runs at its address: the test
//   below checks that replacement against the original (its source, with PLZCW/PMAXW, is not linked);
// - 00586208 / 00586398 (_sceErxExecModuleFunction / _sceErxExecGpFunction, POR as a 128-bit move) are replaced by
//   runtime stubs that stop the game (runtime/sdk.cpp: no ERX modules natively): nothing to compare;
// - 001C1B78 takes 10 arguments (the bridge passes 8);
// - the functions on interrupts, $gp, the kernel or hardware registers (001C2E90, 004A50C0, 004AB9C0, 004EFAA8,
//   0054D248..0054D530, 0054FEC8, 00550188, 005504C8, 00556488, 0055E6D0, 00562D90, 00576A48..00578828, 0057FEA8,
//   00580360, 00586180, 00586190, 005861D0, 00590B80, 00590FD8, 005AE3A0..005AE8B8, 005B0BB0, 005B8B68, 005B9390,
//   005B94A0) and 0049D328 (packets through callbacks): their native state (the runtime kernel, the thread's $gp)
//   is not the original's; check them in a run.
#include "difftest.h"

#include <cstdint>
#include <cstring>

using namespace difftest;

namespace {

void put32(std::uint32_t at, std::uint32_t v) { std::memcpy(ram(at), &v, 4); }
void put16(std::uint32_t at, std::uint16_t v) { std::memcpy(ram(at), &v, 2); }

// a double's bits: mostly ordinary values, some zeros, infinities, NaNs, denormals and huge or tiny exponents
std::uint64_t random_double() {
    const std::uint32_t pick = rnd() % 16;
    const std::uint64_t sign = static_cast<std::uint64_t>(rnd() & 1) << 63;
    const std::uint64_t mant = (static_cast<std::uint64_t>(rnd()) << 32 | rnd()) & 0xFFFFFFFFFFFFFull;
    if (pick == 0) return sign;
    if (pick == 1) return sign | 0x7FF0000000000000ull;
    if (pick == 2) return sign | 0x7FF0000000000000ull | (mant | 1);
    if (pick == 3) return sign | mant;
    if (pick == 4) return sign | (static_cast<std::uint64_t>(1 + rnd() % 2046) << 52) | mant;
    return sign | (static_cast<std::uint64_t>(1023 - 40 + rnd() % 80) << 52) | mant;
}

// 0057F578 dpadd (a, b): two doubles as bits; b often close to a or -a (cancellation, carries, equal exponents)
void two_doubles(std::uint32_t a[8]) {
    const std::uint64_t x = random_double();
    std::uint64_t y = random_double();
    const std::uint32_t pick = rnd() % 4;
    if (pick == 0) y = x ^ (static_cast<std::uint64_t>(rnd() & 1) << 63) ^ (rnd() % 8);   // +-x, last bits changed
    if (pick == 1) y = (x & 0xFFF0000000000000ull) | (y & 0xFFFFFFFFFFFFFull);           // same sign and exponent
    a[0] = static_cast<std::uint32_t>(x);
    a[1] = static_cast<std::uint32_t>(x >> 32);
    a[2] = static_cast<std::uint32_t>(y);
    a[3] = static_cast<std::uint32_t>(y >> 32);
}

// 001C1EF8 (plane, width, rows): rows 16-aligned
void plane_sum(std::uint32_t a[8]) {
    fill(kScratch, 0x10000);
    a[0] = kScratch + 16 * (rnd() % 16);
    a[1] = rnd() % 8 ? rnd() % 200 : 0;
    a[2] = rnd() % 8 ? rnd() % 40 : 0;
}
// 001C1F50 (plane, width, unused, x, y, w, h): a rectangle of it
void rect_sum(std::uint32_t a[8]) {
    fill(kScratch, 0x10000);
    a[0] = kScratch + 16 * (rnd() % 16);
    a[1] = 1 + rnd() % 200;
    a[2] = rnd();
    a[3] = rnd() % a[1];
    a[4] = rnd() % 24;
    a[5] = rnd() % 8 ? rnd() % (a[1] - a[3] + 1) : 0;
    a[6] = rnd() % 8 ? rnd() % 16 : 0;
}
// 001C1FB0 (dst, src, n): 16-aligned buffers, n a multiple of 128
void block_copy(std::uint32_t a[8]) {
    fill(kScratch, 0x8000);
    a[0] = kScratch + 16 * (rnd() % 64);
    a[1] = kScratch + 0x4000 + 16 * (rnd() % 64);
    a[2] = 128 * (rnd() % 24);
}
// 0054F2C0 (dst, src, n): any alignment and length
void byte_copy(std::uint32_t a[8]) {
    fill(kScratch, 0x2000);
    a[0] = kScratch + rnd() % 64;
    a[1] = kScratch + 0x1000 + rnd() % 64;
    a[2] = rnd() % 4 ? rnd() % 0x400 : rnd() % 16;
}

// 003E7A58 RaceSmoke::update (pool, dt): a pool of 64-byte puffs linked by 13-bit indices (-1 ends), the free list
// head, and the per-type parameters at cfg + type * 0x44 + 0x4E0..0x4FC
void smoke_pool(std::uint32_t a[8]) {
    const std::uint32_t pool = kScratch, recs = kScratch + 0x100, cfg = kScratch + 0x2000;
    const int n = 1 + rnd() % 48;
    fill(kScratch, 0x8000);
    fill_floats(cfg, 0x1600, -2.0f, 2.0f);
    put32(pool, recs);
    put32(pool + 0x38, cfg);
    put16(pool + 6, static_cast<std::uint16_t>(rnd() % 64));   // the free list head
    // a random order of a random subset of the 64 records
    int order[64];
    for (int k = 0; k < 64; ++k) order[k] = k;
    for (int k = 63; k > 0; --k) {
        const int j = static_cast<int>(rnd() % static_cast<std::uint32_t>(k + 1));
        const int t = order[k];
        order[k] = order[j];
        order[j] = t;
    }
    for (int k = 0; k < n; ++k) {
        const std::uint32_t r = recs + 64 * static_cast<std::uint32_t>(order[k]);
        const std::uint32_t link = k + 1 < n ? static_cast<std::uint32_t>(order[k + 1]) : 0x1FFFu;
        put32(r, (rnd() & ~0x1FFFu) | link);                        // flags and type above the link
        ramf(r + 0x08)[0] = rndf(0.1f, 5.0f);                       // size
        ramf(r + 0x0C)[0] = rnd() % 8 ? rndf(0.05f, 3.0f) : rndf(-0.5f, 0.05f);   // life
        ramf(r + 0x14)[0] = rndf(-20.0f, 380.0f);                   // angle
        ramf(r + 0x18)[0] = rndf(-5.0f, 5.0f);                      // vel
        ramf(r + 0x24)[0] = rndf(-5.0f, 5.0f);                      // height
        ramf(r + 0x2C)[0] = rndf(-8.0f, 2.0f);                      // ground
    }
    put16(pool + 4, static_cast<std::uint16_t>(order[0]));      // the live list head
    a[0] = pool;
    a[1] = bits(rndf(0.001f, 0.1f));
}

const Register g1({"0057F578", 0x0057F578, "ww", 'w', false, two_doubles});
const Register g2({"001C1EF8", 0x001C1EF8, "iii", 'i', false, plane_sum});
const Register g3({"001C1F50", 0x001C1F50, "iiiiiii", 'i', false, rect_sum});
const Register g4({"001C1FB0", 0x001C1FB0, "iii", 'v', false, block_copy});
const Register g5({"0054F2C0", 0x0054F2C0, "iii", 'v', false, byte_copy});
const Register g6({"003E7A58", 0x003E7A58, "if", 'v', true, smoke_pool});

}  // namespace
