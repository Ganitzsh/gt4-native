// Differential tests of batch A2's native rewrites (game functions that used VU0 macro instructions), run by
// difftest.cpp. Not tested here: 00450558 and 00456918 (their results go to the shared vectors and to globals
// outside the scratch area, which the harness does not compare; 00450558 also sets render state through a dozen
// calls) and 00455708 (a display-program interpreter calling dozens of render functions and the environment's
// virtual hooks).
#include "difftest.h"

#include <cstdint>
#include <cstring>

using namespace difftest;

namespace {

constexpr std::uint32_t kSpr = 0x70002000;   // the graphics library's block in the scratchpad

void put32(std::uint32_t at, std::uint32_t v) { std::memcpy(ram(at), &v, 4); }

// func_0049E3E0, which the projections call first: with no view update pending (byte 0), it only loads shared
// vectors 20..23 from the scratchpad (0xFA0..0xFDF)
void view_block() {
    ram(kSpr)[0] = 0;
    fill_floats(kSpr + 0xFA0, 16, -2.0f, 2.0f);
}
// the view in shared vectors 24..27 with w well away from 0 for points within +-2
void view_rows(float wlo, float whi) {
    random_vectors(24, 27, -1.0f, 1.0f);
    for (int r = 24; r <= 26; ++r) vec(r)[3] = rndf(-0.3f, 0.3f);
    vec(27)[3] = rndf(wlo, whi);
}

// 0044DCC0 (vp, screen, p): p projected through 24..27, clamped by 22, to vp's pixels (width, height at 0x30).
void project_to_viewport(std::uint32_t a[8]) {
    fill(kScratch, 0x300);
    a[0] = kScratch;
    put32(kScratch + 0x30, 100 + rnd() % 900);
    put32(kScratch + 0x34, 100 + rnd() % 900);
    a[1] = kScratch + 0x100;
    a[2] = kScratch + 0x200 + 4 * (rnd() % 4);
    fill_floats(a[2], 3, -2.0f, 2.0f);
    view_block();
    view_rows(4.0f, 8.0f);
}

// 00451238 (pmodel, min, max): the box of a model's bound points through 28..31. *pmodel -> Model (+0x18) ->
// ModelData (+0x38) -> Bound {u8, u8 count, pad, points}.
void bound_box(std::uint32_t a[8]) {
    fill(kScratch, 0x1100);
    a[0] = kScratch;
    put32(kScratch, rnd() % 16 == 0 ? 0 : kScratch + 0x100);
    put32(kScratch + 0x100 + 0x18, kScratch + 0x200);
    put32(kScratch + 0x200 + 0x38, kScratch + 0x300);
    const std::uint32_t n = 1 + rnd() % 40;
    ram(kScratch + 0x300)[1] = static_cast<std::uint8_t>(n);
    const std::uint32_t points = kScratch + 0x400 + 4 * (rnd() % 4);
    put32(kScratch + 0x304, points);
    fill_floats(points, 3 * n, -100.0f, 100.0f);
    a[1] = kScratch + 0x1000 + 4 * (rnd() % 4);
    a[2] = kScratch + 0x1040 + 4 * (rnd() % 4);
    random_vectors(28, 31, -1.5f, 1.5f);
}

// 004522D8 (o, p, i): p projected as above replaces entry i's screen position (o + 0x94 + i * 0x38, +0x30 and +0x34);
// returns func_0057D488(-20 * the distance moved).
void screen_motion(std::uint32_t a[8]) {
    fill_floats(kScratch, 0x100, -1.5f, 1.5f);
    a[0] = kScratch;
    a[1] = kScratch + 0x800 + 4 * (rnd() % 4);
    fill_floats(a[1], 3, -2.0f, 2.0f);
    a[2] = rnd() % 8;
    view_block();
    view_rows(4.0f, 8.0f);
}

// 00454F40 (p): the w of p through 24..27, at least 0.0001 (w often below it here).
void point_depth(std::uint32_t a[8]) {
    fill(kScratch, 0x40);
    a[0] = kScratch + 4 * (rnd() % 4);
    fill_floats(a[0], 3, -2.0f, 2.0f);
    view_block();
    view_rows(-2.0f, 6.0f);
}

// 0048EA48 (out, a, b): out = b's rows through a's rows; out sometimes a or b.
void matrix_product(std::uint32_t a[8]) {
    fill_floats(kScratch, 48, -2.0f, 2.0f);
    a[1] = kScratch;
    a[2] = kScratch + 0x40;
    const std::uint32_t pick = rnd() % 4;
    a[0] = pick == 0 ? a[1] : pick == 1 ? a[2] : kScratch + 0x80;
}

const Register a2_0(Test{"0044DCC0", 0x0044DCC0, "iii", 'v', true, project_to_viewport});
const Register a2_1(Test{"00451238", 0x00451238, "iii", 'v', true, bound_box});
const Register a2_2(Test{"004522D8", 0x004522D8, "iii", 'f', true, screen_motion});
const Register a2_3(Test{"00454F40", 0x00454F40, "i", 'f', true, point_depth});
const Register a2_4(Test{"0048EA48", 0x0048EA48, "iii", 'v', true, matrix_product});

}  // namespace
