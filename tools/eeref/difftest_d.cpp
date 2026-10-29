// Differential tests of batch D's native rewrites (game_src/func_004A*.cpp: the graphics library's sine and
// cosine, clipping flags, lights and arctangent) against the original code. See difftest.h.
//
// Not tested here (the harness saves, restores and compares only its scratch area and the return):
// - 004A5348, 004A6C58, 004A6E20, 004A70E8, 004ABD40 read and write the graphics library's state in the
//   scratchpad (0x70002000; 004A6C58 and 004A70E8 build GS packets there): check with recorded calls;
// - 004A53F8, 004A5400 (matrix push / pop), 004AC41C (random seed) leave their results only in VU0's registers
//   and data memory, which the harness does not compare;
// - 004A20C8 waits on hardware registers.
#include "difftest.h"

#include "native/vu0.h"

#include <cstdint>

using namespace difftest;

namespace {

// 004A5EB0 / 004A5ED8 (angle): sin / cos of an angle in degrees.
void angle_only(std::uint32_t a[8]) {
    a[0] = bits(rndf(-720.0f, 720.0f));
}

// 004A5F00 (s, c, angle): both, stored through two pointers (any alignment).
void sin_cos_out(std::uint32_t a[8]) {
    fill(kScratch, 0x100);
    a[0] = kScratch + 0x10 + 4 * (rnd() % 4);
    a[1] = kScratch + 0x40 + 4 * (rnd() % 4);
    a[2] = bits(rndf(-720.0f, 720.0f));
}

// 004A72F0 (p): clipping flags of a 4-float point (any alignment), w of either sign.
void clip_point(std::uint32_t a[8]) {
    fill(kScratch, 0x100);
    a[0] = kScratch + 0x10 + 4 * (rnd() % 4);
    fill_floats(a[0], 4, -10.0f, 10.0f);
    if (rnd() % 8 == 0) ramf(a[0])[0] = ramf(a[0])[3];   // on a plane
}

// 004ABEC0 (s, l, which, v): a light parameter; 5 and 13 go through camera matrix set 0 (shared vectors 28..31
// when s->current is 0, else the copy at s + 0xED0). v is a 128-bit argument (four words).
void light_param(std::uint32_t a[8]) {
    fill(kScratch, 0x1100);
    a[0] = kScratch;               // s: current at +1, flags at +0x48, matrix at +0xED0
    ram(kScratch)[1] = static_cast<std::uint8_t>(rnd() % 2);
    fill_floats(kScratch + 0xED0, 16, -1.5f, 1.5f);
    a[1] = kScratch + 0x1000;      // l: enabled at +8, parameters at +0x10..+0x90
    ram(a[1])[8] = static_cast<std::uint8_t>(rnd() % 2);
    static const std::uint32_t kWhich[] = {0, 1, 4, 5, 13, 13, 5, 2, 20};
    a[2] = kWhich[rnd() % (sizeof kWhich / sizeof kWhich[0])];
    for (int k = 0; k < 4; ++k) a[3 + k] = bits(rndf(-10.0f, 10.0f));
    if (rnd() % 2) a[6] = bits(0.0f);   // a direction
    random_vectors(28, 31, -1.5f, 1.5f);
}

// 004AC118 (y, x): atan2, the axes included.
void atan2_args(std::uint32_t a[8]) {
    a[0] = bits(rnd() % 8 == 0 ? 0.0f : rndf(-100.0f, 100.0f));
    a[1] = bits(rnd() % 8 == 0 ? 0.0f : rndf(-100.0f, 100.0f));
}

// 004AC400 (): the next random number from VU0's R register.
void random_state(std::uint32_t[8]) {
    __gt4_vu0.r = 0x3F800000u | (rnd() & 0x7FFFFFu);
}

const Register d0(Test{"004A5EB0", 0x004A5EB0, "f", 'f', true, angle_only});
const Register d1(Test{"004A5ED8", 0x004A5ED8, "f", 'f', true, angle_only});
const Register d2(Test{"004A5F00", 0x004A5F00, "iif", 'f', true, sin_cos_out});
const Register d3(Test{"004A72F0", 0x004A72F0, "i", 'i', false, clip_point});
const Register d4(Test{"004ABEC0", 0x004ABEC0, "iiiq", 'v', true, light_param});
const Register d5(Test{"004AC118", 0x004AC118, "ff", 'f', true, atan2_args});
const Register d6(Test{"004AC400", 0x004AC400, "", 'f', false, random_state});

}  // namespace
