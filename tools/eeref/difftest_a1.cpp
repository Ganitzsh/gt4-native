// Differential tests of batch A1's native VU0 rewrites (game_src/func_ADDR.cpp; docs/VU0_REWRITE.md): see
// difftest.h. Functions left out, and why:
//   003905B0, 00390980  car model objects (virtuals, the car, the course's ground query) and the matrix stack
//   003B8EA0, 003D51B0  the matrix stack, packet allocation and drawing
//   003E2710            the course's runway search (func_003E29F8)
//   003E3290            writes only shared vectors 20..23, which a test does not compare; 003E32E8 below runs it
//                       and uses what it leaves
//   003E7F08, 003E8790  GS state, the packet buffer and many drawing callees (003E8438 is tested on its own)
#include "difftest.h"

#include <cstdint>
#include <cstring>

using namespace difftest;

namespace {

void put32(std::uint32_t at, std::uint32_t v) { std::memcpy(ram(at), &v, 4); }

// a random direction of length about 1
void unit(std::uint32_t at) {
    float *v = ramf(at);
    float len2 = 0.0f;
    while (len2 < 0.01f) {
        for (int k = 0; k < 3; ++k) v[k] = rndf(-1.0f, 1.0f);
        len2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
    }
    float len = 1.0f;   // a few Newton steps: good enough for a test input
    for (int n = 0; n < 8; ++n) len = 0.5f * (len + len2 / len);
    for (int k = 0; k < 3; ++k) v[k] /= len;
}

// D_006D6054 (the body shape table, 160 bytes an entry) pointed at four random entries in the scratch area:
// floats in [-1, 1], inertia [1..3] in [0.5, 3], mass [0x25] in [500, 2000], the flags byte and the point count.
constexpr std::uint32_t kTable = kScratch + 0x1000;
void body_table() {
    put32(0x006D6054, kTable);
    for (std::uint32_t e = 0; e < 4; ++e) {
        const std::uint32_t entry = kTable + e * 160;
        fill_floats(entry, 40, -1.0f, 1.0f);
        const std::uint32_t data = entry + 32;
        for (int k = 1; k <= 3; ++k) ramf(data)[k] = rndf(0.5f, 3.0f);
        ramf(data)[0x25] = rndf(500.0f, 2000.0f);
        ram(data)[2] = static_cast<std::uint8_t>(rnd());   // flags
        ram(data)[3] = static_cast<std::uint8_t>(rnd() % 8);   // point count (003E3458)
    }
}

// a body (003E3840, 003E3ED0, 003E34E8...): floats in [-2, 2], shape index at 0x44 (0..3)
void body(std::uint32_t at) {
    fill_floats(at, 0x60 / 4, -2.0f, 2.0f);
    ram(at)[0x44] = static_cast<std::uint8_t>(rnd() % 4);
    ram(at)[0x45] = ram(at)[0x46] = ram(at)[0x47] = 0;
}

// 003B7DD8 (self, dir, v, power): v through rows 28..30, its cosine with dir raised to round(power).
void cosine_power(std::uint32_t a[8]) {
    fill(kScratch, 0x100);
    a[0] = kScratch;
    a[1] = kScratch + 0x40 + 4 * (rnd() % 4);
    fill_floats(a[1], 3, -1.0f, 1.0f);
    a[2] = kScratch + 0x80 + 4 * (rnd() % 4);
    fill_floats(a[2], 3, -1.0f, 1.0f);
    a[3] = bits(rndf(-2.0f, 40.0f));
    random_vectors(28, 30, -1.5f, 1.5f);
}

// 003B8530 (self, light, alpha, flag): a light added to the flare list. type 1: func_003B7EE8 (a scene query) is
// not reached.
void add_flare(std::uint32_t a[8]) {
    fill(kScratch, 0x2400);
    a[0] = kScratch;
    put32(kScratch + 0xA30, 1);              // type
    put32(kScratch + 0xA34, rnd() % 101);    // count (100: full)
    ramf(kScratch + 0x21B0)[0] = rndf(0.5f, 3.0f);   // the camera of func_003B7C98
    ramf(kScratch + 0x21C4)[0] = rndf(0.5f, 3.0f);
    ramf(kScratch + 0x21D0)[0] = rndf(-0.5f, 0.5f);
    ramf(kScratch + 0x21D4)[0] = rndf(-0.5f, 0.5f);
    fill_floats(kScratch + 0x21F0, 15, 0.0f, 1.0f);   // parameters
    ramf(kScratch + 0x2200)[0] = rndf(0.5f, 2.0f);    // sy: radius scale
    ramf(kScratch + 0x2218)[0] = rndf(50.0f, 400.0f); // p28: near
    ramf(kScratch + 0x2228)[0] = rndf(0.0f, 0.2f);    // p38: power scale
    const std::uint32_t l = kScratch + 0x2300;
    a[1] = l;
    fill_floats(l, 0x60 / 4, -1.0f, 1.0f);
    ramf(l + 0x40)[0] = rndf(0.5f, 3.0f);                                          // width
    ramf(l + 0x44)[0] = rnd() % 3 == 0 ? ramf(l + 0x40)[0] : rndf(0.5f, 3.0f);    // height
    fill_floats(l + 0x48, 3, -60.0f, 60.0f);                                       // origin
    put32(l + 0x60, rnd() % 40);                                                   // power
    a[2] = bits(rndf(0.0f, 1.0f));
    a[3] = rnd() % 2;
    random_vectors(28, 31, -1.5f, 1.5f);
}

// 003E2220 (out, in): in through rows 24..26. Unaligned three-float points.
void rotate_24(std::uint32_t a[8]) {
    fill(kScratch, 0x100);
    a[0] = kScratch + 0x10 + 4 * (rnd() % 4);
    a[1] = kScratch + 0x40 + 4 * (rnd() % 4);
    fill_floats(a[1], 3, -1000.0f, 1000.0f);
    random_vectors(24, 26, -2.0f, 2.0f);
}

// 003E32E8 (body, vel, world, local): the body's matrix (003E3290 from its angles at 0x4), local through it and the
// body's velocity there.
void body_point(std::uint32_t a[8]) {
    fill(kScratch, 0x200);
    fill_floats(kScratch, 0x34 / 4, -10.0f, 10.0f);
    fill_floats(kScratch + 4, 3, -3.14159f, 3.14159f);   // angles
    a[0] = kScratch;
    a[1] = kScratch + 0x100;
    a[2] = kScratch + 0x140 + 4 * (rnd() % 4);
    a[3] = kScratch + 0x180 + 4 * (rnd() % 4);
    fill_floats(a[3], 3, -3.0f, 3.0f);
    random_vectors(20, 23, -2.0f, 2.0f);   // overwritten by 003E3290
}

// 003E3458 (body, out): the body's shape points through rows 20..23.
void shape_points(std::uint32_t a[8]) {
    fill(kScratch, 0x400);
    body_table();
    body(kScratch);
    a[0] = kScratch;
    a[1] = kScratch + 0x200 + 4 * (rnd() % 4);
    random_vectors(20, 23, -2.0f, 2.0f);
}

// 003E34E8 (self, out, unused, points, count): ground contact forces of up to seven points.
void ground_push(std::uint32_t a[8]) {
    fill(kScratch, 0x400);
    body_table();
    body(kScratch);
    unit(kScratch + 0x34);                          // the ground normal
    ramf(kScratch + 0x40)[0] = rndf(-1.0f, 1.0f);   // its distance
    a[0] = kScratch;
    a[1] = kScratch + 0x300;
    a[2] = rnd();
    a[3] = kScratch + 0x200;
    a[4] = rnd() % 8;
    fill_floats(a[3], 3 * 7, -2.0f, 2.0f);
    fill_floats(a[1], 3, -10.0f, 10.0f);            // out: the forces so far (not NaN bit patterns)
    random_vectors(20, 23, -1.0f, 1.0f);
}

// 003E3840 (body, index, normal): a collision impulse at contact point index.
void contact_impulse(std::uint32_t a[8]) {
    fill(kScratch, 0x400);
    body_table();
    body(kScratch);
    a[0] = kScratch;
    a[1] = rnd() % 7;
    a[2] = kScratch + 0x200;
    unit(a[2]);
    random_vectors(20, 23, -1.0f, 1.0f);
}

// 003E3ED0 (body, index, side, hit, other): an impulse against another object (its motion at 0x454..0x46C, its
// spin at 0x480); the normal from the game's table D_00621D28 (func_003E43B8).
void object_impulse(std::uint32_t a[8]) {
    fill(kScratch, 0x1000);
    body_table();
    body(kScratch);
    a[0] = kScratch;
    a[1] = rnd() % 7;
    a[2] = rnd() % 2;
    a[3] = kScratch + 0x200;
    fill_floats(a[3], 4, -2.0f, 2.0f);
    a[4] = kScratch + 0x400;
    fill_floats(a[4] + 0x454, 6, -2.0f, 2.0f);
    ramf(a[4] + 0x480)[0] = rndf(-1.0f, 1.0f);
    random_vectors(20, 23, -1.0f, 1.0f);
}

// 003E8438 (obj, pos, st, rgba0, rgba2, n): a GS packet of n vertices at obj's cursor (0x58). func_0049E3E0 loads
// rows 20..23 from the scratchpad (0x70002FA0; its flag byte at 0x70002000 cleared so it does not recompute them).
void vertex_packet(std::uint32_t a[8]) {
    fill(kScratch, 0x1200);
    a[0] = kScratch;
    put32(kScratch + 0x58, kScratch + 0x1000);
    a[1] = kScratch + 0x200;
    a[5] = 1 + rnd() % 6;
    fill_floats(a[1], 3 * a[5] + 1, -50.0f, 50.0f);
    a[2] = kScratch + 0x400;
    a[3] = rnd();
    a[4] = rnd();
    ram(0x70002000)[0] = 0;
    float *spr = ramf(0x70002FA0);
    const float scale[4] = {rndf(200.0f, 400.0f), rndf(200.0f, 400.0f), rndf(1e4f, 1e5f), rndf(10.0f, 100.0f)};
    const float offset[4] = {rndf(1800.0f, 2300.0f), rndf(1800.0f, 2300.0f), rndf(1e5f, 1e6f), rndf(0.0f, 100.0f)};
    for (int k = 0; k < 4; ++k) {
        spr[k] = scale[k];
        spr[4 + k] = offset[k];
        spr[8 + k] = rndf(5.0f, 50.0f);   // the clamp (MINI)
        spr[12 + k] = rndf(-1.0f, 1.0f);
    }
    random_vectors(24, 27, -1.5f, 1.5f);
}

const Register a1_r0(Test{"003B7DD8", 0x003B7DD8, "iiif", 'f', true, cosine_power});
const Register a1_r1(Test{"003B8530", 0x003B8530, "iifi", 'v', true, add_flare});
const Register a1_r2(Test{"003E2220", 0x003E2220, "ii", 'v', true, rotate_24});
const Register a1_r3(Test{"003E32E8", 0x003E32E8, "iiii", 'v', true, body_point});
const Register a1_r4(Test{"003E3458", 0x003E3458, "ii", 'v', true, shape_points});
const Register a1_r5(Test{"003E34E8", 0x003E34E8, "iiiii", 'v', true, ground_push});
const Register a1_r6(Test{"003E3840", 0x003E3840, "iii", 'i', true, contact_impulse});
const Register a1_r7(Test{"003E3ED0", 0x003E3ED0, "iiiii", 'f', true, object_impulse});
const Register a1_r8(Test{"003E8438", 0x003E8438, "iiiiii", 'v', true, vertex_packet});

}  // namespace
