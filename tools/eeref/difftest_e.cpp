// Differential tests of batch E's native rewrites of MMI functions (layer 3a: PLZCW as plain C); see difftest.h.
//
// Not tested here:
// - 00129010, 001AB110, 001BE458, 00331E80, 00393790, 003A8968, 003AED70, 003AEF18, 003AF108, 003AF2F0, 003AF4F8,
//   003C5AB8, 003D7930, 0042DE50 draw a texture through the graphics library: they load it (00499728 / 004997B0,
//   the texture manager and VRAM) and build GS packets from its state, behind objects with virtual tables; PLZCW
//   only gives the power of two at or above the texture's extents. Check with recorded calls;
// - 00572C40 only stores the constant 5 in D_008744CC, outside the compared regions.
#include "difftest.h"

#include <cstdint>
#include <cstring>

using namespace difftest;

namespace {

void put32(std::uint32_t at, std::uint32_t v) { std::memcpy(ram(at), &v, 4); }

// a 32-bit value of any bit length (zero and negative included)
std::uint32_t any_length() {
    const std::uint32_t k = rnd() % 34;
    if (k == 32) return 0;
    if (k == 33) return 0xFFFFFFFFu;
    return rnd() >> k;
}

// a 64-bit value of any bit length
std::uint64_t any_length64() {
    const std::uint32_t k = rnd() % 66;
    if (k == 64) return 0;
    const std::uint64_t x = (static_cast<std::uint64_t>(rnd()) << 32) | rnd();
    if (k == 65) return x | 0x8000000000000000ull;
    return x >> k;
}

// 00545A54 (p): 32 words, the top ones zero (all of them at times), the top bit of the first nonzero one clear or set
void bit_length(std::uint32_t a[8]) {
    fill(kScratch, 0x100);
    const std::uint32_t p = kScratch + 0x10 + 4 * (rnd() % 4);
    const std::uint32_t zeros = rnd() % 8 ? rnd() % 33 : 32;
    for (std::uint32_t k = 0; k < zeros; ++k) put32(p + 4 * (31 - k), 0);
    if (zeros < 32) put32(p + 4 * (31 - zeros), any_length() | (rnd() % 2 ? 1u : 0u));
    a[0] = p;
}

// 00572438 (unused, n): the base-2 logarithm of n rounded up, less D_008744CC, clamped to 0..15
void log_level(std::uint32_t a[8]) {
    a[0] = rnd();
    a[1] = rnd() % 4 ? 1 + rnd() % 0x20000 : any_length();
}

// 00578598 (state): one step of the generator 00579330 (the word at state) as a float
void random_float(std::uint32_t a[8]) {
    fill(kScratch, 0x40);
    a[0] = kScratch + 4 * (rnd() % 8);
    if (rnd() % 8 == 0) put32(a[0], rnd() % 2 ? 0xF0F0F0EFu : 0xFFFFFFFFu);   // values whose next step is 0 or small
}

// 0057F490 (value, sign) / 0057F4F8 (value, sign): a 64-bit magnitude and a sign as a double / a float
void magnitude_double(std::uint32_t a[8]) {
    const std::uint64_t x = any_length64();
    a[0] = static_cast<std::uint32_t>(x);
    a[1] = static_cast<std::uint32_t>(x >> 32);
    a[2] = rnd() % 2 ? rnd() % 2 : rnd();
    a[3] = rnd() % 2 ? 0 : rnd();
}
void magnitude_float(std::uint32_t a[8]) {
    const std::uint64_t x = any_length64();
    a[0] = static_cast<std::uint32_t>(x);
    a[1] = static_cast<std::uint32_t>(x >> 32);
    a[2] = rnd() % 2 ? rnd() % 2 : rnd();
}

// 005E3538 (unused, n): n over the power of two at or above it
void over_power(std::uint32_t a[8]) {
    a[0] = rnd();
    a[1] = rnd() % 4 ? 1 + rnd() % 0x20000 : any_length();
}

const Register e1({"00545A54", 0x00545A54, "i", 'i', false, bit_length});
const Register e2({"00572438", 0x00572438, "ii", 'i', false, log_level});
const Register e3({"00578598", 0x00578598, "i", 'f', false, random_float});
const Register e4({"0057F490", 0x0057F490, "ww", 'w', false, magnitude_double});
const Register e5({"0057F4F8", 0x0057F4F8, "wi", 'f', true, magnitude_float});   // int to float: EE and x87 rounding
const Register e6({"005E3538", 0x005E3538, "ii", 'f', true, over_power});         // unsigned to float above 2^24

}  // namespace
