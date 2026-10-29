// Differential tests of batch B's native rewrites (the graphics library's VU0 functions, 0049xxxx); see difftest.h.
//
// Not tested here: 004961B8, 00496638, 00499948 and 00499D00 write their GS packets at the scratchpad's cursor
// (0x700021F8, directly or through 004A5B90), which a setup cannot point at the compared area for both runs (the
// native run advances it); they are checked with recorded calls. 004998A8 writes only shared vectors (16..23), which
// the harness does not compare yet: its test runs both versions but checks nothing until it does.
#include "difftest.h"

#include <cstdint>
#include <cstring>

using namespace difftest;

namespace {

constexpr std::uint32_t kSpr = 0x70002000;   // the graphics library's scratchpad work area

// the 2D projection the sprite path sets up (004998A8): rows 16, 17, 19 with w lanes giving w = 1, row 18 = 0;
// 20 a scale, 21 an offset (screen centre in GS coordinates), 22 a clamp above every lane
void sprite_projection() {
    random_vectors(16, 19, -2.0f, 2.0f);
    for (int k = 0; k < 4; ++k) vec(18)[k] = 0.0f;
    vec(16)[3] = 0.0f;
    vec(17)[3] = 0.0f;
    vec(19)[3] = rndf(0.5f, 2.0f);
    random_vectors(20, 20, 0.5f, 2.0f);
    random_vectors(21, 21, 1500.0f, 2500.0f);
    random_vectors(22, 22, 1.0e5f, 1.0e6f);
}

// 0049A150 (ctx, flags, n, pos, uv, colors, alpha): the colour scale (+0xFE0, vf15) and one of the four 0x678
// drawing functions, which write a packet at ctx->packet (+0x1F8) from pos (2 floats per vertex), uv (2) and
// colors (4)
void draw_vertices(std::uint32_t a[8]) {
    const std::uint32_t ctx = kScratch, packet = kScratch + 0x2000;
    const std::uint32_t pos = kScratch + 0x8000, uv = kScratch + 0x9000, colors = kScratch + 0xA000;
    fill(kScratch, 0xB000);
    std::memcpy(ram(ctx + 0x1F8), &packet, 4);
    fill_floats(pos, 0x100, -400.0f, 400.0f);
    fill_floats(uv, 0x100, 0.0f, 1.0f);
    fill_floats(colors, 0x100, -0.2f, 1.2f);
    a[0] = ctx;
    a[1] = rnd() & 0x7FF;          // PRIM bits
    a[2] = 1 + rnd() % 8;          // n
    a[3] = pos;
    a[4] = rnd() % 2 ? uv : 0;
    a[5] = rnd() % 2 ? colors : 0;
    a[6] = bits(rndf(0.0f, 1.0f));   // alpha
    random_vectors(15, 15, 0.0f, 255.0f);
    sprite_projection();
}

// 0049B2C8 (x, y, z as float bits, zero-extended as its caller loads them: kind u): the clip code of the point
// through rows 24..27
void clip_point(std::uint32_t a[8]) {
    for (int k = 0; k < 3; ++k) a[k] = bits(rndf(-100.0f, 100.0f));
    random_vectors(24, 27, -1.5f, 1.5f);
}

// 0049C140 (s): matrix (28..31 in mode 2, else +0xF50) into rows 20..23 and a packet at s->cursor (+0x1F8)
void matrix_packet(std::uint32_t a[8]) {
    const std::uint32_t s = kScratch, cursor = kScratch + 0x2000;
    fill(kScratch, 0x2100);
    ram(s)[1] = rnd() % 2 ? 2 : static_cast<std::uint8_t>(rnd());
    std::memcpy(ram(s + 0x1F8), &cursor, 4);
    fill_floats(s + 0xF50, 16, -100.0f, 100.0f);
    random_vectors(28, 31, -100.0f, 100.0f);
    a[0] = s;
}

// 0049CFE8 (s): matrix (28..31 in mode 2, else +0xF50) and +0xB00 pushed below s->top (+0x1FC), DMA tag at +0x30
void matrix_push(std::uint32_t a[8]) {
    const std::uint32_t s = kScratch, top = kScratch + 0x3000;
    fill(kScratch, 0x3100);
    ram(s)[1] = rnd() % 2 ? 2 : static_cast<std::uint8_t>(rnd());
    std::memcpy(ram(s + 0x1FC), &top, 4);
    fill_floats(s + 0xB00, 4, -100.0f, 100.0f);
    fill_floats(s + 0xF50, 16, -100.0f, 100.0f);
    random_vectors(28, 31, -100.0f, 100.0f);
    a[0] = s;
}

// 004998A8 (v): rows 16..23 from three points and rows 24, 25, 27 (outputs: shared vectors only, see above)
void sprite_setup(std::uint32_t a[8]) {
    fill(kScratch, 0x100);
    a[0] = kScratch + 0x10 + 4 * (rnd() % 4);
    fill_floats(a[0], 9, -2.0f, 2.0f);
    ram(kSpr)[0] = 0;   // no view update (0049E480)
    fill_floats(kSpr + 0xFA0, 16, -1000.0f, 1000.0f);
    random_vectors(24, 27, -2.0f, 2.0f);
}

// 00496EF0 (in, sx, sy): a point through rows 24..27 to the screen (rows 20..22 from the scratchpad at +0xFA0 by
// 0049E3E0), *sx and *sy relative to the scratchpad's offsets (+0x26C, +0x26E); returns the depth
void project_screen(std::uint32_t a[8]) {
    fill(kScratch, 0x100);
    a[0] = kScratch + 0x10 + 4 * (rnd() % 4);
    a[1] = kScratch + 0x40;
    a[2] = kScratch + 0x50;
    fill_floats(a[0], 3, -100.0f, 100.0f);
    ram(kSpr)[0] = 0;                                   // no view update (0049E480)
    ram(kSpr)[7] = static_cast<std::uint8_t>(rnd() % 2);   // sy divided by 8, or 16 when 0
    const std::uint16_t offx = static_cast<std::uint16_t>(0x6000 + rnd() % 0x2000);
    const std::uint16_t offy = static_cast<std::uint16_t>(0x6000 + rnd() % 0x2000);
    std::memcpy(ram(kSpr + 0x26C), &offx, 2);
    std::memcpy(ram(kSpr + 0x26E), &offy, 2);
    fill_floats(kSpr + 0xFA0, 4, 100.0f, 400.0f);      // vf20: scale
    fill_floats(kSpr + 0xFB0, 4, 1500.0f, 2500.0f);    // vf21: offset
    fill_floats(kSpr + 0xFC0, 4, 1.0e5f, 1.0e6f);      // vf22: clamp
    fill_floats(kSpr + 0xFD0, 4, -1.0f, 1.0f);         // vf23
    random_vectors(24, 27, -1.5f, 1.5f);
    for (int r = 24; r <= 26; ++r) vec(r)[3] = rndf(-0.3f, 0.3f);
    vec(27)[3] = rndf(100.0f, 200.0f);                  // w well above 0
}

const Register b0(Test{"0049A150", 0x0049A150, "iiiiiif", 'v', true, draw_vertices});
const Register b1(Test{"0049B2C8", 0x0049B2C8, "uuu", 'i', true, clip_point});
const Register b2(Test{"0049C140", 0x0049C140, "i", 'v', false, matrix_packet});
const Register b3(Test{"0049CFE8", 0x0049CFE8, "i", 'v', false, matrix_push});
const Register b4(Test{"004998A8", 0x004998A8, "i", 'v', true, sprite_setup});
const Register b5(Test{"00496EF0", 0x00496EF0, "iii", 'f', true, project_screen});

}  // namespace
