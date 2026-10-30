// Differential tests of the functions changed when the VU0 model left the game (docs/NATIVE_ONLY.md): the 2D drawing
// helpers that ran VU0 microprogram 0x678, the matrix rotations (programs 0x78, 0xB8, 0xF8), the matrix stack (0x900,
// 0x930), the random seed, and the functions whose recovered sources still had VU0 asm. The original code runs the
// microprograms in the VU's arithmetic (tools/eeref/vu0_programs.cpp). See difftest.h.
//
// Not tested here: RaceCarModel__virtual_37 / _41 (00390660, 003908C8) build their matrix through a car model's
// object graph (vtables, 004596A8, 00450FB0); they read shared vector 29 / 31 after the tested matrix helpers.
#include "difftest.h"

#include "shared_state.h"

#include <cstdint>
#include <cstring>

using namespace difftest;

namespace {

constexpr std::uint32_t kSpr = 0x70002000;   // the scratchpad work area (spr_base())

void put32(std::uint32_t at, std::uint32_t v) { std::memcpy(ram(at), &v, 4); }
void put16(std::uint32_t at, std::uint16_t v) { std::memcpy(ram(at), &v, 2); }
void put8(std::uint32_t at, std::uint8_t v) { ram(at)[0] = v; }
void putf(std::uint32_t at, float f) { ramf(at)[0] = f; }

// ---- the 2D drawing helpers (0049A200 ...): microprogram 0x678 ----

// The projection they read: rows 16, 17, 19 (row 18 meets the z the original passes, a GPR's stale upper half on the
// first vertex; the graphics library keeps it 0, 00499F18), scale 20, offset 21, clamp 22 (sometimes the graphics
// library's default 0x7FFFFFFF, no clamp), colour scale 15. w stays well away from 0.
void projection_2d() {
    random_vectors(15, 22, -2.0f, 2.0f);
    for (int k = 0; k < 4; ++k) vec(15)[k] = rndf(0.0f, 255.0f);
    for (int k = 0; k < 4; ++k) vec(18)[k] = 0.0f;
    vec(16)[3] = rndf(-0.0005f, 0.0005f);   // w = row16.w x + row17.w y + row19.w >= 0.2 for |x|, |y| <= 300
    vec(17)[3] = rndf(-0.0005f, 0.0005f);
    vec(19)[3] = rndf(0.5f, 2.0f);
    for (int k = 0; k < 4; ++k) {
        vec(19)[k] = k == 3 ? vec(19)[3] : rndf(-50.0f, 50.0f);
        vec(20)[k] = rndf(0.5f, 2.0f);
        vec(21)[k] = rndf(1000.0f, 3000.0f);
        vec(22)[k] = rnd() % 3 == 0 ? rndf(-100.0f, 400.0f) : 0.0f;
        if (rnd() % 3 != 0) std::memcpy(&vec(22)[k], "\xFF\xFF\xFF\x7F", 4);
    }
}
// ctx at kScratch: prim +0x4A, packet cursor +0x1F8 (at kScratch + 0x40000), colours +0xC20 / +0xC24; n points at
// +0x20000, texture coordinates at +0x24000, colours (four floats) at +0x28000; any word alignment.
std::uint32_t draw_2d(std::uint32_t a[8]) {
    fill(kScratch, 0x1000);
    fill(kScratch + 0x40000, 0x2000);
    put32(kScratch + 0x1F8, kScratch + 0x40000 + 4 * (rnd() % 4));
    const std::uint32_t n = 1 + rnd() % 12;
    a[0] = kScratch;
    a[1] = rnd() & 0xFFFF;
    a[2] = n;
    a[3] = kScratch + 0x20000 + 4 * (rnd() % 4);
    a[4] = kScratch + 0x24000 + 4 * (rnd() % 4);
    a[5] = kScratch + 0x28000 + 4 * (rnd() % 4);
    fill_floats(a[3], 2 * n, -300.0f, 300.0f);
    fill_floats(a[4], 2 * n, 0.0f, 1.0f);
    fill_floats(a[5], 4 * n, -0.2f, 1.2f);
    projection_2d();
    return n;
}
void draw_2d_pos(std::uint32_t a[8]) { draw_2d(a); }
void draw_2d_pos_colours(std::uint32_t a[8]) {   // (ctx, flags, n, pos, colors)
    draw_2d(a);
    a[4] = a[5];
}

// ---- the matrix helpers ----

// 004A7988 / 004A79B0 / 004A79D8 (angle): the current matrix (28..31) turned about x / y / z.
void rotate(std::uint32_t a[8]) {
    random_vectors(28, 31, -2.0f, 2.0f);
    a[0] = bits(rndf(-720.0f, 720.0f));
}

// The matrix stack: random rows and top.
void random_stack() {
    for (auto &row : gt4_matrix_stack)
        for (float &f : row) f = rndf(-2.0f, 2.0f);
    gt4_matrix_stack_top = static_cast<std::uint16_t>(rnd() % 3 == 0 ? rnd() : rnd() % 0x60);
}
// 004A53F8 / 004A5400 (): push / pop the current matrix.
void push_pop(std::uint32_t[8]) {
    random_stack();
    random_vectors(28, 31, -2.0f, 2.0f);
    put8(kSpr, 0);
}
// 004A5348 (index): makes a matrix set current: the scratchpad's sets at +0xED0, their tops at +0xF90, the current
// one at +1.
void matrix_set(std::uint32_t a[8]) {
    random_stack();
    random_vectors(28, 31, -2.0f, 2.0f);
    put8(kSpr + 1, static_cast<std::uint8_t>(rnd() % 4));
    for (int k = 0; k < 48; ++k) putf(kSpr + 0xED0 + 4 * k, rndf(-2.0f, 2.0f));
    for (int k = 0; k < 3; ++k) put16(kSpr + 0xF90 + 2 * k, static_cast<std::uint16_t>(rnd()));
    a[0] = rnd() % 4;
}

// 004AC41C (seed): the random state from a float's mantissa.
void seed(std::uint32_t a[8]) {
    gt4_random_state = 0x3F800000u | (rnd() & 0x7FFFFFu);
    a[0] = rnd();
}

// ---- the recovered sources with VU0 asm ----

// 0048D3E0 ... (out, a, b): 16-byte vectors; out may be a.
void vec_op(std::uint32_t a[8]) {
    fill(kScratch, 0x100);
    fill_floats(kScratch, 0x30 / 4, -100.0f, 100.0f);
    a[1] = kScratch + 0x10;
    a[2] = kScratch + 0x20;
    a[0] = rnd() % 4 == 0 ? a[1] : kScratch + 0x40;
}
// 0048D680 ... (out, i, a, b): arrays of 16-byte vectors.
void vec_op_indexed(std::uint32_t a[8]) {
    fill(kScratch, 0x400);
    fill_floats(kScratch, 0x300 / 4, -100.0f, 100.0f);
    a[0] = kScratch + 0x300 - 0x100;
    a[1] = rnd() % 8;
    a[2] = kScratch;
    a[3] = kScratch + 0x100;
}
// 0048EB10 (m, out, v): v through the rows of m.
void mat_vec(std::uint32_t a[8]) {
    fill(kScratch, 0x100);
    fill_floats(kScratch, 0x50 / 4, -10.0f, 10.0f);
    a[0] = kScratch;
    a[1] = kScratch + 0x80;
    a[2] = kScratch + 0x40;
}

// 00499F18 (s, v): the 2D projection from three points; s clean (0049E480 not run), screen rows at +0xFA0.
void projection_setup(std::uint32_t a[8]) {
    fill(kScratch, 0x1000);
    put8(kScratch, 0);
    fill_floats(kScratch + 0xFA0, 16, -500.0f, 500.0f);
    a[0] = kScratch;
    a[1] = kScratch + 0x100 + 4 * (rnd() % 4);
    fill_floats(a[1], 9, -300.0f, 300.0f);
    random_vectors(16, 27, -2.0f, 2.0f);
}

// 0049E228 (src): a matrix to the scratchpad, scaled by D_00624CC0's rows when enabled.
constexpr std::uint32_t kScaleGlobal = 0x00624CC0;
void matrix_to_spr(std::uint32_t a[8]) {
    fill(kScratch, 0x100);
    fill_floats(kScratch, 17, -2.0f, 2.0f);
    put8(kScratch + 0x44, static_cast<std::uint8_t>(rnd()));
    for (int k = 0; k < 16; ++k) putf(kScaleGlobal + 0x20 + 4 * k, rndf(0.5f, 2.0f));
    put8(kScaleGlobal + 0x60, static_cast<std::uint8_t>(rnd() % 2));
    put16(kSpr + 0x48, static_cast<std::uint16_t>(rnd()));
    a[0] = kScratch;
    random_vectors(8, 8, -2.0f, 2.0f);
}

// 0049E430 (): the view rows (20..23) from the current matrix or set 0's copy.
void view_load(std::uint32_t[8]) {
    put8(kSpr + 1, static_cast<std::uint8_t>(rnd() % 3));
    for (int k = 0; k < 16; ++k) putf(kSpr + 0xED0 + 4 * k, rndf(-2.0f, 2.0f));
    random_vectors(20, 31, -2.0f, 2.0f);
}

// 0049E548 (s, reg, scale): the view matrix (24..27, scaled) and four rows to the packet.
void view_packet(std::uint32_t a[8]) {
    fill(kScratch, 0x1000);
    fill(kScratch + 0x40000, 0x200);
    put32(kScratch + 0x1F8, kScratch + 0x40000);
    fill_floats(kScratch + 0xFA0, 16, -2.0f, 2.0f);
    a[0] = kScratch;
    a[1] = rnd();
    a[2] = bits(rnd() % 4 == 0 ? 0.0f : rndf(0.1f, 4.0f));
    random_vectors(24, 27, -2.0f, 2.0f);
}

// The light list of 0049EE00 (and of 0049EB90 through it): 0..4 lights at kScratch + 0x2000, every other one a
// direction light (pos.w 0), index -2..7.
void light_list(std::uint32_t s) {
    const int nodes = static_cast<int>(rnd() % 5);
    std::uint32_t next = 0;
    for (int n = nodes - 1; n >= 0; --n) {
        const std::uint32_t at = kScratch + 0x2000 + 0xC0 * static_cast<std::uint32_t>(n);
        fill(at, 0xC0);
        put32(at, next);
        put8(at + 9, static_cast<std::uint8_t>(static_cast<int>(rnd() % 10) - 2));
        fill_floats(at + 0x60, 20, -2.0f, 2.0f);
        if (rnd() % 2) putf(at + 0x6C, 0.0f);
        next = at;
    }
    put32(s + 0xB18, next);
}
// 0049EE00 (s): the lights in view space.
void lights_view(std::uint32_t a[8]) {
    fill(kScratch, 0x1000);
    a[0] = kScratch;
    put8(kScratch + 1, static_cast<std::uint8_t>(rnd() % 2));
    put8(kScratch + 0xE, static_cast<std::uint8_t>(rnd() % 2));
    fill_floats(kScratch + 0xAB0, 4, -10.0f, 10.0f);
    fill_floats(kScratch + 0xBE0, 4, 0.5f, 2.0f);
    fill_floats(kScratch + 0xED0, 16, -2.0f, 2.0f);
    random_vectors(20, 31, -2.0f, 2.0f);
    light_list(kScratch);
}
// 0049EB90 (s, scale): six quadwords on the packet stack (+0x1FC at kScratch + 0x8000); flags 0 or 2 (0049EE00).
void light_packet(std::uint32_t a[8]) {
    lights_view(a);
    fill(kScratch + 0x7E00, 0x200);
    put32(kScratch + 0x1FC, kScratch + 0x8000);
    put16(kScratch + 0x48, static_cast<std::uint16_t>(rnd() % 2 ? 2 : 0));
    fill_floats(kScratch + 0x2E0, 12, -1.0f, 1.0f);
    fill_floats(kScratch + 0x3A0, 8, -1.0f, 1.0f);
    fill_floats(kScratch + 0xAC0, 4, -10.0f, 10.0f);
    putf(kScratch + 0xAA0, rndf(0.0f, 20.0f));
    putf(kScratch + 0xB10, rndf(0.0f, 2.0f));
    putf(kScratch + 0xB14, rndf(0.0f, 128.0f));
    a[1] = bits(rnd() % 4 == 0 ? 0.0f : rndf(0.5f, 4.0f));
    random_vectors(8, 19, -2.0f, 2.0f);
}

// 0049F658 (x, y, z): the roll from the eye direction and the rows at D_0084AFC0 (compared) from those at
// D_0084AFF0..D_0084B040. The w lanes of the rotation rows are stale VU0 lanes in the original (vf4..vf6, which the
// bridge gives it from shared vectors 4..6): 0, as the rewrite takes them.
void eye_roll(std::uint32_t a[8]) {
    for (std::uint32_t at = 0x0084AFF0; at < 0x0084B050; at += 4) putf(at, rndf(-1.0f, 1.0f));
    random_vectors(4, 6, -2.0f, 2.0f);
    for (int r = 4; r <= 6; ++r) vec(r)[3] = 0.0f;
    a[0] = bits(rndf(-10.0f, 10.0f));
    a[1] = bits(rndf(-10.0f, 10.0f));
    a[2] = bits(rndf(-10.0f, 10.0f));
}

// 003E33C0 (body, out, a, local): a x (local through the body's transposed rotation; 003E3290 from its angles).
void body_cross(std::uint32_t a[8]) {
    fill(kScratch, 0x200);
    fill_floats(kScratch, 0x34 / 4, -10.0f, 10.0f);
    fill_floats(kScratch + 4, 3, -3.14159f, 3.14159f);   // angles
    a[0] = kScratch;
    a[1] = kScratch + 0x100 + 4 * (rnd() % 4);
    a[2] = kScratch + 0x140 + 4 * (rnd() % 4);
    a[3] = kScratch + 0x180 + 4 * (rnd() % 4);
    fill_floats(a[2], 3, -3.0f, 3.0f);
    fill_floats(a[3], 3, -3.0f, 3.0f);
    random_vectors(20, 23, -2.0f, 2.0f);
}

const Register v0(Test{"0049A200", 0x0049A200, "iiii", 'v', true, draw_2d_pos});
const Register v1(Test{"0049A360", 0x0049A360, "iiiii", 'v', true, draw_2d_pos});
const Register v2(Test{"0049A4F8", 0x0049A4F8, "iiiii", 'v', true, draw_2d_pos_colours});
const Register v3(Test{"0049A6A0", 0x0049A6A0, "iiiiii", 'v', true, draw_2d_pos});
const Register v4(Test{"004A7988", 0x004A7988, "f", 'v', true, rotate});
const Register v5(Test{"004A79B0", 0x004A79B0, "f", 'v', true, rotate});
const Register v6(Test{"004A79D8", 0x004A79D8, "f", 'v', true, rotate});
const Register v7(Test{"004A53F8", 0x004A53F8, "", 'v', false, push_pop});
const Register v8(Test{"004A5400", 0x004A5400, "", 'v', false, push_pop});
const Register v9(Test{"004A5348", 0x004A5348, "i", 'v', false, matrix_set});
const Register v10(Test{"004AC41C", 0x004AC41C, "f", 'v', false, seed});
const Register v11(Test{"0048D3E0", 0x0048D3E0, "iii", 'v', true, vec_op});
const Register v12(Test{"0048D408", 0x0048D408, "iii", 'v', true, vec_op});
const Register v13(Test{"0048D430", 0x0048D430, "iii", 'v', true, vec_op});
const Register v14(Test{"0048D4C0", 0x0048D4C0, "iii", 'v', true, vec_op});
const Register v15(Test{"0048D530", 0x0048D530, "iii", 'v', true, vec_op});
const Register v16(Test{"0048D680", 0x0048D680, "iiii", 'v', true, vec_op_indexed});
const Register v17(Test{"0048D6C0", 0x0048D6C0, "iiii", 'v', true, vec_op_indexed});
const Register v18(Test{"0048D700", 0x0048D700, "iiii", 'v', true, vec_op_indexed});
const Register v19(Test{"0048DF98", 0x0048DF98, "iiii", 'v', true, vec_op_indexed});
const Register v20(Test{"0048DFD8", 0x0048DFD8, "iiii", 'v', true, vec_op_indexed});
const Register v21(Test{"0048E018", 0x0048E018, "iiii", 'v', true, vec_op_indexed});
const Register v22(Test{"0048EB10", 0x0048EB10, "iii", 'v', true, mat_vec});
const Register v23(Test{"00499F18", 0x00499F18, "ii", 'v', true, projection_setup});
const Register v24(Test{"0049E228", 0x0049E228, "i", 'v', true, matrix_to_spr});
const Register v25(Test{"0049E430", 0x0049E430, "", 'v', false, view_load});
const Register v26(Test{"0049E548", 0x0049E548, "iif", 'v', true, view_packet});
const Register v27(Test{"0049EB90", 0x0049EB90, "if", 'v', true, light_packet, 0, 0, 11, 0xC});
const Register v28(Test{"0049EE00", 0x0049EE00, "i", 'v', true, lights_view});
const Register v29(Test{"0049F658", 0x0049F658, "fff", 'v', true, eye_roll, 0x0084AFC0, 48});
const Register v30(Test{"003E33C0", 0x003E33C0, "iiii", 'v', true, body_cross});

}  // namespace
