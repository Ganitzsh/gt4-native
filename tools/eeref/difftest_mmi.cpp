// Differential tests of the native rewrites of MMI functions (layer 3a: 128-bit integer instructions as plain C);
// see difftest.h.
//
// Not tested here: 0034D490, 00363B10 and 003FCA08 read a point of the loaded course (003952C0), checked with
// recorded calls.
#include "difftest.h"

#include <cstdint>
#include <cstring>

using namespace difftest;

namespace {

void put32(std::uint32_t at, std::uint32_t v) { std::memcpy(ram(at), &v, 4); }
void put16(std::uint32_t at, std::uint16_t v) { std::memcpy(ram(at), &v, 2); }
void putf(std::uint32_t at, float v) { std::memcpy(ram(at), &v, 4); }

// 001FDD28 / 001FDE68 (s, pixels): s->height (+0x90) a multiple of 64, s->width (+0x94); the alpha bytes sit at
// pixels + height/2*width*4
void alpha_expand(std::uint32_t a[8], bool even) {
    const std::uint32_t s = kScratch, p = kScratch + 0x100;
    fill(kScratch, 0x2000);
    put32(s + 0x90, 64 * (1 + rnd() % 2));
    put32(s + 0x94, even ? 2 * (1 + rnd() % 2) : 1 + rnd() % 4);
    a[0] = s;
    a[1] = p;
}
void alpha_expand_a(std::uint32_t a[8]) { alpha_expand(a, false); }
void alpha_expand_b(std::uint32_t a[8]) { alpha_expand(a, true); }

// 00330E88 (dst, x, y, src, alpha, row, sx, sy): two images (data +4, depth +0x10, height +0x14, width +0x18,
// channels +0x20) with 16-aligned data in the scratch area
void blend_images(std::uint32_t a[8]) {
    const std::uint32_t dst = kScratch, src = kScratch + 0x40;
    fill(kScratch, 0x10000);
    const std::uint32_t depth = rnd() % 8 ? 1 : 2;
    put32(dst + 4, kScratch + 0x1000);
    put32(src + 4, kScratch + 0x8000);
    put32(dst + 0x10, depth);
    put32(src + 0x10, rnd() % 16 ? depth : depth + 1);
    const std::uint32_t src_h = 1 + rnd() % 8, row = rnd() % 4;
    put32(src + 0x14, src_h);
    put32(dst + 0x14, src_h + row + rnd() % 8 - 1);
    put32(dst + 0x18, 1 + rnd() % 4);
    put32(src + 0x18, 1 + rnd() % 4);
    put32(dst + 0x20, 4);
    put32(src + 0x20, 4);
    a[0] = dst;
    a[1] = rnd() % 3;
    a[2] = rnd() % 3;
    a[3] = src;
    a[4] = rnd() % 8 ? rnd() % 257 : rnd() % 1024;
    a[5] = row;
    a[6] = rnd() % 3;
    a[7] = rnd() % 3;
}

// 00498E48 (o): blends two of o->frames (+0xC, 8 bytes each, data at +4) into o->out (+8), o->count (+4) colours,
// when o->dirty (+0x1F)
void blend_frames(std::uint32_t a[8]) {
    const std::uint32_t o = kScratch, frames = kScratch + 0x100;
    fill(kScratch, 0x3000);
    put16(o + 4, rnd() % 40);
    put32(o + 8, kScratch + 0x1000);
    put32(o + 0xC, frames);
    put16(o + 0x10, rnd() % 4);
    put16(o + 0x12, rnd() % 4);
    putf(o + 0x18, rnd() % 16 ? rndf(0.0f, 1.0f) : rndf(-2.0f, 2.0f));
    ram(o + 0x1F)[0] = rnd() % 8 ? 1 : 0;
    for (std::uint32_t i = 0; i < 4; ++i) put32(frames + i * 8 + 4, kScratch + 0x2000 + i * 0x100);
    a[0] = o;
}

// 0049F938 (s, tag, mode): a GS packet at s->cursor (+0x1F8) with colour s->colors[mode != 0] (+0xC20)
void colour_packet(std::uint32_t a[8]) {
    fill(kScratch, 0x2000);
    put32(kScratch + 0x1F8, kScratch + 0x1000);
    a[0] = kScratch;
    a[1] = rnd() & 0x3FF;
    a[2] = rnd() % 2;
}

// 0056BA50 (a, b, out, n): n/4 quads of words, every value (saturation included)
void difference(std::uint32_t a[8]) {
    fill(kScratch, 0x4000);
    a[0] = kScratch + 0x1000;
    a[1] = kScratch + 0x2000;
    a[2] = kScratch + 0x3000;
    a[3] = 4 + rnd() % 60;
    if (rnd() % 2)
        for (std::uint32_t k = 0; k < 0x100; ++k) {   // values near the range, where the rounding shows
            put32(a[0] + k * 4, rnd() % 0x2000000 - 0x1000000);
            put32(a[1] + k * 4, rnd() % 0x2000000 - 0x1000000);
        }
}

// 0049C2A8 / 0049C788 (verts, colors, normals, count, first): a triangle-list packet at the graphics library's
// state in the scratchpad (0x70002000; cursor +0x1F8); the pointers only go into DMA tags
void triangles(std::uint32_t a[8]) {
    const std::uint32_t s = 0x70002000;
    fill(s, 0x1100);
    ram(s)[0] = 0;                                            // no flush (0049E480)
    ram(s)[3] = rnd() % 2;
    putf(s + 0x20, rnd() % 2 ? 0.0f : rndf(-1.0f, 1.0f));
    put32(s + 0x40, rnd() % 4 ? rnd() : 0x3f3f3f3f);
    put32(s + 0x1F8, kScratch + 0x1000);
    fill(kScratch, 0x2000);
    fill_floats(s + 0xF50, 16, -100.0f, 100.0f);
    random_vectors(28, 31, -100.0f, 100.0f);
    a[0] = kScratch + 0x100;
    a[1] = rnd() % 2 ? kScratch + 0x200 : 0;
    a[2] = rnd() % 2 ? kScratch + 0x300 : 0;
    a[3] = 1 + rnd() % 24;
    a[4] = rnd() % 3 - 1;
}

const Register r1({"001FDD28", 0x001FDD28, "ii", 'v', false, alpha_expand_a});
const Register r2({"001FDE68", 0x001FDE68, "ii", 'v', false, alpha_expand_b});
const Register r3({"00330E88", 0x00330E88, "iiiiiiii", 'v', false, blend_images});
const Register r4({"00498E48", 0x00498E48, "i", 'v', false, blend_frames});
const Register r5({"0049F938", 0x0049F938, "iii", 'v', false, colour_packet});
const Register r6({"0056BA50", 0x0056BA50, "iiii", 'v', false, difference});
const Register r7({"0049C2A8", 0x0049C2A8, "iiiii", 'v', false, triangles});
const Register r8({"0049C788", 0x0049C788, "iiiii", 'v', false, triangles});

}  // namespace
