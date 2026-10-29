// Differential tests of batch F's native rewrites (the maths functions whose sources used the EE FPU's accumulator
// and cvt.w.s / cvt.s.w in asm: logf, expf, atanf, cosf, sinf, tanf, ceilf, floorf, sincosf); see difftest.h.
//
// The inputs keep away from where the EE and x87 cannot agree bit for bit: results past FLT_MAX (the EE's exponent
// 255 is an ordinary one: expf of 88.03..88.72, tanf of an exact multiple of pi/2 give 0x7F8xxxxx / 0xFFFFFFFF
// there, FLT_MAX natively) and trigonometry of large arguments (the EE's subtraction drops the bits it shifts out, so
// x - n pi can differ by an ulp of x: past |x| = 64 that is more than the drift tolerance). Ceil and floor are
// exact; their exponent-255 inputs are quiet patterns (x87 quiets a signalling NaN it loads).
//
// Not tested here: the SDK functions of the same batch (0057FEA8, 00580360, 00586180, 00586190, 005861D0,
// 00586208, 00586398, 00590B80, 00590FD8, 005B0BB0, 005B8B68, 005B9390, 005B94A0) are runtime territory.
#include "difftest.h"

#include <cstdint>

using namespace difftest;

namespace {

// one of `edges` (bit patterns) one time in `one_in`, else uniform in [lo, hi]
std::uint32_t pick(const std::uint32_t *edges, std::uint32_t count, std::uint32_t one_in, float lo, float hi) {
    if (rnd() % one_in == 0) return edges[rnd() % count];
    return bits(rndf(lo, hi));
}
// a float of any exponent from `lo_exp` to `hi_exp` (biased), either sign
std::uint32_t any_magnitude(std::uint32_t lo_exp, std::uint32_t hi_exp) {
    return (rnd() & 0x80000000u) | ((lo_exp + rnd() % (hi_exp - lo_exp + 1)) << 23) | (rnd() & 0x7FFFFFu);
}
#define COUNT(a) static_cast<std::uint32_t>(sizeof(a) / sizeof((a)[0]))

// zeros, denormals, the smallest normals, one
const std::uint32_t kSmall[] = {0x00000000u, 0x80000000u, 0x00000001u, 0x807FFFFFu, 0x00400000u, 0x00800000u,
                                0x80800000u, 0x0DA24260u, 0x3F800000u, 0xBF800000u};

// 004A8AD0 (x): logf. Every bit pattern is a finite input (the function reads only the bits).
void log_arg(std::uint32_t a[8]) {
    const std::uint32_t k = rnd() % 4;
    if (k == 0) a[0] = rnd();
    else if (k == 1) a[0] = any_magnitude(1, 254) & 0x7FFFFFFFu;
    else if (k == 2) a[0] = kSmall[rnd() % COUNT(kSmall)];
    else a[0] = bits(rndf(0.0f, 10.0f));
}

// 004A8BB0 (x): expf, below the overflow, past the underflow; the image's largest float (D_006AFC08) above.
void exp_arg(std::uint32_t a[8]) {
    static const std::uint32_t edges[] = {0x00000000u, 0x80000000u, 0x00000001u, 0x807FFFFFu, 0x3F800000u,
                                          0xBF800000u, 0x42B00000u /* 88 */, 0xC2B00000u, 0x42C80000u /* 100 */,
                                          0xC2C80000u, 0x7F7FFFFFu, 0xFF7FFFFFu, 0x7F800000u, 0xFF800000u,
                                          0x7FFFFFFFu, 0xFFFFFFFFu, 0x447A0000u /* 1000 */};
    a[0] = rnd() % 2 ? pick(edges, COUNT(edges), 4, -100.0f, 88.0f) : bits(rndf(-5.0f, 5.0f));
}

// 0057D008 (x): atanf of any magnitude, the region bounds included.
void atan_arg(std::uint32_t a[8]) {
    static const std::uint32_t edges[] = {0x00000000u, 0x80000000u, 0x00000001u, 0x807FFFFFu, 0x3ED413CDu,
                                          0xBED413CDu, 0x401A827Au, 0xC01A827Au, 0x3F800000u, 0xBF800000u,
                                          0x7F7FFFFFu, 0xFF7FFFFFu, 0x7F800000u, 0xFF800000u, 0x7FFFFFFFu};
    const std::uint32_t k = rnd() % 3;
    if (k == 0) a[0] = edges[rnd() % COUNT(edges)];
    else if (k == 1) a[0] = any_magnitude(1, 254);
    else a[0] = bits(rndf(-10.0f, 10.0f));
}

// 0057D1F0 / 0057D2B8 / 0057D8A0: cosf, sinf, sincosf within a few turns, sometimes up to +-60.
const std::uint32_t kTrig[] = {0x00000000u, 0x80000000u, 0x00000001u, 0x807FFFFFu, 0x0DA24260u, 0x3FC90FDBu,
                               0xBFC90FDBu, 0x40490FDBu, 0xC0490FDBu, 0x3F490FDBu, 0xBF490FDBu, 0x40C90FDBu,
                               0x3F800000u, 0xBF800000u};
std::uint32_t trig_value() {
    if (rnd() % 4 == 0) return pick(kTrig, COUNT(kTrig), 2, -60.0f, 60.0f);
    return pick(kTrig, COUNT(kTrig), 8, -7.0f, 7.0f);
}
void trig_arg(std::uint32_t a[8]) { a[0] = trig_value(); }

// 0057D380: tanf, both sides of pi/4 (the reduction), not at the poles.
void tan_arg(std::uint32_t a[8]) {
    static const std::uint32_t edges[] = {0x00000000u, 0x80000000u, 0x00000001u, 0x807FFFFFu, 0x3F490FDBu,
                                          0xBF490FDBu, 0x3F800000u, 0xBF800000u, 0x40490FDBu, 0xC0490FDBu};
    if (rnd() % 4 == 0) a[0] = pick(edges, COUNT(edges), 2, -10.0f, 10.0f);
    else a[0] = pick(edges, COUNT(edges), 8, -3.0f, 3.0f);
}

// 0057D6E8 / 0057D738: ceilf, floorf: halves and integers, past 2^23, any pattern (signalling NaNs made quiet).
void round_arg(std::uint32_t a[8]) {
    static const std::uint32_t edges[] = {0x00000000u, 0x80000000u, 0x00000001u, 0x807FFFFFu, 0x3F000000u,
                                          0xBF000000u, 0x3F800000u, 0xBF800000u, 0x4AFFFFFFu, 0xCAFFFFFFu,
                                          0x4B000000u, 0xCB000000u, 0x4B000001u, 0x7F7FFFFFu, 0xFF7FFFFFu,
                                          0x7F800000u, 0xFF800000u, 0x7FFFFFFFu, 0xFFFFFFFFu};
    const std::uint32_t k = rnd() % 5;
    if (k == 0) a[0] = edges[rnd() % COUNT(edges)];
    else if (k == 1) a[0] = bits(static_cast<float>(static_cast<int>(rnd() % 2001) - 1000) * (rnd() % 2 ? 1.0f : 0.5f));
    else if (k == 2) {
        a[0] = rnd();
        if ((a[0] & 0x7F800000u) == 0x7F800000u) a[0] |= 0x00400000u;
    } else a[0] = bits(rndf(-1000.0f, 1000.0f));
}

// 0057D8A0 (sine, cosine, x): two words in the scratch area (any alignment; sometimes the same word, where the sine
// is stored last).
void sincos_args(std::uint32_t a[8]) {
    fill(kScratch, 0x100);
    a[0] = kScratch + 0x10 + 4 * (rnd() % 4);
    a[1] = rnd() % 16 == 0 ? a[0] : kScratch + 0x40 + 4 * (rnd() % 4);
    a[2] = trig_value();
}

const Register f0(Test{"004A8AD0", 0x004A8AD0, "f", 'f', true, log_arg});
const Register f1(Test{"004A8BB0", 0x004A8BB0, "f", 'f', true, exp_arg});
const Register f2(Test{"0057D008", 0x0057D008, "f", 'f', true, atan_arg});
const Register f3(Test{"0057D1F0", 0x0057D1F0, "f", 'f', true, trig_arg});
const Register f4(Test{"0057D2B8", 0x0057D2B8, "f", 'f', true, trig_arg});
const Register f5(Test{"0057D380", 0x0057D380, "f", 'f', true, tan_arg});
const Register f6(Test{"0057D6E8", 0x0057D6E8, "f", 'f', false, round_arg});
const Register f7(Test{"0057D738", 0x0057D738, "f", 'f', false, round_arg});
const Register f8(Test{"0057D8A0", 0x0057D8A0, "iif", 'v', true, sincos_args});

}  // namespace
