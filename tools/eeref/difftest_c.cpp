// Differential tests of batch C's native rewrites (game_src/func_0049D580.cpp ... func_0049FFA0.cpp: the graphics
// library's skinned vertex batches, light packets and view matrices). See difftest.h.
//
// Not tested here, since run() compares only the scratch area and the return value: 0049E3E0 (its only output is
// shared vectors 20..23), 0049F4C8 and 0049F7D8 (their outputs are game globals, D_0084AF80 / D_0084AFF0 ...,
// and shared vectors 20..23; 0049F7D8 also calls 0049F658). The shared vectors the tested functions also leave
// (bones 8..23 from 0049D970, the view matrix 24..27 from 0049E480, 8..18 from the light packets, 20..23 from
// 0049F1F8) are not compared either.
#include "difftest.h"

#include <cstdint>
#include <cstring>

using namespace difftest;

namespace {

constexpr std::uint32_t kSpr = 0x70002000;   // the scratchpad work area (spr_base())

void put32(std::uint32_t at, std::uint32_t v) { std::memcpy(ram(at), &v, 4); }
void put16(std::uint32_t at, std::uint16_t v) { std::memcpy(ram(at), &v, 2); }
void put8(std::uint32_t at, std::uint8_t v) { ram(at)[0] = v; }
void putf(std::uint32_t at, float f) { ramf(at)[0] = f; }

// The context the skinning functions take: command word +0x40, packet cursor +0x1F8 (at kScratch + 0x40000, any
// word alignment), bone table +0xC34 (256 random 64-byte matrices at kScratch + 0x10000; sometimes none, then
// 0049D970 loads identities).
void skin_context() {
    fill(kScratch, 0x1000);
    fill(kScratch + 0x40000, 0x2000);
    put32(kScratch + 0x1F8, kScratch + 0x40000 + 4 * (rnd() % 4));
    if (rnd() % 6 == 0) {
        put32(kScratch + 0xC34, 0);
    } else {
        put32(kScratch + 0xC34, kScratch + 0x10000);
        fill_floats(kScratch + 0x10000, 256 * 16, -1.0f, 1.0f);
    }
}

// A command stream at kScratch + 0x20000: 1..3 groups of a header (count | more), a skipped field, four bone
// indices and `count` vertices of `size` bytes made by `vertex`; returns the total vertex count.
template <class Header>
std::uint32_t skin_stream(std::uint32_t size, Header more, void (*vertex)(std::uint32_t at)) {
    std::uint32_t at = kScratch + 0x20000, total = 0;
    fill(at, 0x2000);
    const int groups = 1 + static_cast<int>(rnd() % 3);
    for (int g = 0; g < groups; ++g) {
        const std::uint32_t count = rnd() % 6;
        total += count;
        const Header hdr = static_cast<Header>(count | (g + 1 < groups ? more : 0));
        std::memcpy(ram(at), &hdr, sizeof hdr);
        at += 2 * sizeof hdr;   // the header and the field the bone loader ignores (left random)
        at += 4;                // the bone indices (random)
        for (std::uint32_t k = 0; k < count; ++k, at += size) vertex(at);
    }
    return total;
}

void float_vertex(std::uint32_t at) {   // 0049D580: position, four weights
    fill_floats(at, 3, -5.0f, 5.0f);
    fill_floats(at + 12, 4, 0.0f, 1.0f);
}
void float_vertex_normal(std::uint32_t at) {   // 0049D728: position, normal, four weights
    fill_floats(at, 3, -5.0f, 5.0f);
    fill_floats(at + 12, 3, -1.0f, 1.0f);
    fill_floats(at + 24, 4, 0.0f, 1.0f);
}
void packed_vertex(std::uint32_t) {}   // 0049DB08 / 0049DCB0: random bytes (the stream is filled first)

// 0049D970 (s, p): four bone matrices into shared vectors 8..23; the return (p + 4) is compared.
void bone_load(std::uint32_t a[8]) {
    skin_context();
    a[0] = kScratch;
    a[1] = kScratch + 0x100 + rnd() % 16;
    fill(kScratch + 0x100, 0x20);
}
// 0049D580 (ctx, cmd, n): float positions skinned by VU0 program 0x478.
void skin_float(std::uint32_t a[8]) {
    skin_context();
    a[0] = kScratch;
    a[1] = kScratch + 0x20000;
    a[2] = skin_stream<std::uint16_t>(0x1C, 0x8000, float_vertex);
}
// 0049D728 (s, cmd, count, dest): float positions and normals, VU0 program 0x308.
void skin_float_normal(std::uint32_t a[8]) {
    skin_context();
    a[0] = kScratch;
    a[1] = kScratch + 0x20000;
    a[2] = skin_stream<std::uint16_t>(0x28, 0x8000, float_vertex_normal);
    a[3] = rnd() & 0x3FF;
}
// 0049DB08 (ctx, cmd, n, scale): 16-bit positions and byte weights, VU0 program 0x460.
void skin_packed(std::uint32_t a[8]) {
    skin_context();
    a[0] = kScratch;
    a[1] = kScratch + 0x20000;
    a[2] = skin_stream<std::uint8_t>(10, 0x80, packed_vertex);
    a[3] = bits(rndf(0.00005f, 0.00015f));
}
// 0049DCB0 (s, cmd, count, dest, scale): 16-bit positions, byte normals and weights, VU0 program 0x2E8.
void skin_packed_normal(std::uint32_t a[8]) {
    skin_context();
    a[0] = kScratch;
    a[1] = kScratch + 0x20000;
    a[2] = skin_stream<std::uint8_t>(13, 0x80, packed_vertex);
    a[3] = rnd() & 0x3FF;
    a[4] = bits(rndf(0.00005f, 0.00015f));
}

// 0049E2D8 (in, out): a point through the view matrix (shared vectors 24..27) and the scratchpad's screen matrix
// (spr +0xFA0, loaded by 0049E3E0; dirty byte 0, so 0049E480 does not run and change the scratchpad).
void screen_point(std::uint32_t a[8]) {
    fill(kScratch, 0x300);
    a[0] = kScratch + 0x100 + 4 * (rnd() % 4);
    fill_floats(a[0], 3, -10.0f, 10.0f);
    a[1] = kScratch + 0x200;
    put8(kSpr, 0);
    put8(kSpr + 7, static_cast<std::uint8_t>(rnd() % 2));
    put16(kSpr + 0x26C, static_cast<std::uint16_t>(rnd()));
    put16(kSpr + 0x26E, static_cast<std::uint16_t>(rnd()));
    fill_floats(kSpr + 0xFA0, 4, 100.0f, 400.0f);     // scale (vf20)
    fill_floats(kSpr + 0xFB0, 4, 1000.0f, 3000.0f);   // offset (vf21)
    fill_floats(kSpr + 0xFC0, 4, 2.0f, 20.0f);        // clamp (vf22)
    fill_floats(kSpr + 0xFD0, 4, -1.0f, 1.0f);
    random_vectors(24, 27, -1.0f, 1.0f);
    for (int r = 24; r <= 26; ++r) vec(r)[3] = rndf(-0.01f, 0.01f);
    vec(27)[3] = rndf(1.0f, 2.0f);
}

// 0049E480 (s): the view matrix (shared vectors 24..27) from the current matrix and the scratch block's two
// matrices; the flags it clears and sets are compared.
void view_refresh(std::uint32_t a[8]) {
    fill(kScratch, 0x1000);
    a[0] = kScratch;
    put8(kScratch + 1, static_cast<std::uint8_t>(rnd() % 4));
    put8(kScratch + 0xE, static_cast<std::uint8_t>(rnd() % 2));
    fill_floats(kScratch + 0xBD0, 4, 0.5f, 2.0f);
    fill_floats(kScratch + 0xED0, 32, -2.0f, 2.0f);
    random_vectors(28, 31, -2.0f, 2.0f);
}

// The packet state the light functions take: stack pointer +0x1FC at kScratch + 0x8000, below it random bytes.
void packet_state() {
    fill(kScratch, 0xC00);
    fill(kScratch + 0x7E00, 0x200);
    put32(kScratch + 0x1FC, kScratch + 0x8000);
}
// 0049E698 (pk): eight lights, the last five scaled, and the colour pushed on the packet stack.
void lights8(std::uint32_t a[8]) {
    packet_state();
    a[0] = kScratch;
    fill_floats(kScratch + 0x2E0, 32, -1.0f, 1.0f);
    putf(kScratch + 0xB10, rndf(0.0f, 2.0f));
    putf(kScratch + 0xB14, rndf(0.0f, 128.0f));
}
// 0049E780 (s): sixteen quadwords (lights, colours, scaled rows) and the VU1 size by distance.
void lights16(std::uint32_t a[8]) {
    packet_state();
    a[0] = kScratch;
    fill_floats(kScratch + 0x2E0, 44, -1.0f, 1.0f);   // +0x2E0 eight rows, +0x360 three
    fill_floats(kScratch + 0x3A0, 16, -1.0f, 1.0f);
    fill_floats(kScratch + 0xAD0, 4, -1.0f, 1.0f);
    putf(kScratch + 0xAA0, rndf(0.0f, 20.0f));
    putf(kScratch + 0xB10, rndf(0.0f, 2.0f));
    putf(kScratch + 0xB14, rndf(0.0f, 128.0f));
}
// 0049E918 (s): nine quadwords (merged halves, scaled rows, the colour) and the VU1 size by distance.
void lights9(std::uint32_t a[8]) {
    packet_state();
    a[0] = kScratch;
    fill_floats(kScratch + 0x310, 12, -1.0f, 1.0f);
    fill_floats(kScratch + 0x3A0, 8, -1.0f, 1.0f);
    putf(kScratch + 0xAA0, rndf(0.0f, 20.0f));
    putf(kScratch + 0xB10, rndf(0.0f, 2.0f));
    putf(kScratch + 0xB14, rndf(0.0f, 128.0f));
}
// 0049F0F0 (s): three quadwords copied to the packet stack.
void copy3(std::uint32_t a[8]) {
    packet_state();
    a[0] = kScratch;
}
// 0049F170 (s): the view matrix and two scaled quadwords to the packet stack.
void view_push(std::uint32_t a[8]) {
    packet_state();
    a[0] = kScratch;
    fill_floats(kScratch + 0xB20, 16, -2.0f, 2.0f);
    fill_floats(kScratch + 0xBA0, 12, -2.0f, 2.0f);
}

// 0049F038 (s): the light list's products and their sum.
void light_list(std::uint32_t a[8]) {
    fill(kScratch, 0xC00);
    a[0] = kScratch;
    fill_floats(kScratch + 0xA60, 16, -2.0f, 2.0f);
    fill_floats(kScratch + 0xAE0, 4, -2.0f, 2.0f);
    if (rnd() % 4 == 0)
        for (int k = 0; k < 3; ++k) put32(kScratch + 0xA80 + 4 * k, 0);
    const int nodes = static_cast<int>(rnd() % 5);
    std::uint32_t next = 0;
    for (int n = nodes - 1; n >= 0; --n) {
        const std::uint32_t at = kScratch + 0x2000 + 0x60 * static_cast<std::uint32_t>(n);
        fill(at, 0x60);
        put32(at, next);
        put8(at + 9, static_cast<std::uint8_t>(static_cast<int>(rnd() % 10) - 2));   // -2..7
        fill_floats(at + 0x10, 20, -2.0f, 2.0f);
        next = at;
    }
    put32(kScratch + 0xB18, next);
}

// 0049F1F8 (s, scale): the inverse camera matrix into s->view (+0xB20). vf4's w is the w of the result's row 3.
void view_inverse(std::uint32_t a[8]) {
    fill(kScratch, 0x1000);
    a[0] = kScratch;
    put8(kScratch + 1, static_cast<std::uint8_t>(rnd() % 2));
    put8(kScratch + 0xE, static_cast<std::uint8_t>(rnd() % 2));
    put8(kScratch + 0xF, static_cast<std::uint8_t>(rnd() % 2));
    fill_floats(kScratch + 0xB60, 12, -1.0f, 1.0f);
    fill_floats(kScratch + 0xBE0, 4, 0.5f, 2.0f);
    fill_floats(kScratch + 0xED0, 16, -2.0f, 2.0f);
    random_vectors(28, 31, -2.0f, 2.0f);
    random_vectors(4, 4, -2.0f, 2.0f);
    a[1] = bits(rnd() % 4 == 0 ? 0.0f : rndf(0.5f, 4.0f));
}

// 0049FFA0 (self): the state block reset (identity matrices and defaults).
void state_reset(std::uint32_t a[8]) {
    fill(kScratch, 0x1000);
    a[0] = kScratch;
}

const Register c0(Test{"0049D970", 0x0049D970, "ii", 'i', false, bone_load});
const Register c1(Test{"0049D580", 0x0049D580, "iii", 'v', true, skin_float});
const Register c2(Test{"0049D728", 0x0049D728, "iiii", 'v', true, skin_float_normal});
const Register c3(Test{"0049DB08", 0x0049DB08, "iiif", 'v', true, skin_packed});
const Register c4(Test{"0049DCB0", 0x0049DCB0, "iiiif", 'v', true, skin_packed_normal});
const Register c5(Test{"0049E2D8", 0x0049E2D8, "ii", 'v', true, screen_point});
const Register c6(Test{"0049E480", 0x0049E480, "i", 'v', true, view_refresh});
const Register c7(Test{"0049E698", 0x0049E698, "i", 'v', true, lights8});
const Register c8(Test{"0049E780", 0x0049E780, "i", 'v', true, lights16});
const Register c9(Test{"0049E918", 0x0049E918, "i", 'v', true, lights9});
const Register c10(Test{"0049F038", 0x0049F038, "i", 'v', true, light_list});
const Register c11(Test{"0049F0F0", 0x0049F0F0, "i", 'v', false, copy3});
const Register c12(Test{"0049F170", 0x0049F170, "i", 'v', true, view_push});
const Register c13(Test{"0049F1F8", 0x0049F1F8, "if", 'v', true, view_inverse});
const Register c14(Test{"0049FFA0", 0x0049FFA0, "i", 'v', false, state_reset});

}  // namespace
