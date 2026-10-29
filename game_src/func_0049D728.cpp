/* compiler: ee-gcc2.96-nsa-nosib-as2004 */
/* Native version of 0049D728 (VU0 asm, VU0 microprogram 0x308): a batch of float vertices (position and normal)
 * skinned by four bones into a VIF packet at s->top (+0x1F8). Header: a DMA cnt tag (its quadword count written
 * last), 0, VIF UNPACK V3-32 of n positions (n << 16 | 0x6800C000), the n positions; 0x20000000, 0xC0C0C0C0,
 * UNPACK of n normals to `dest` (n << 16 | dest | 0x7800C000), the n normals; 0x20000000, s->command (+0x40),
 * zero words to 16 bytes; the cursor moves past it. The command stream: groups of a halfword header (count in
 * bits 0..14, bit 15: another group follows), a halfword the bone loader ignores, four bone index bytes (0049D970
 * loads the bones into shared vectors 8..23), then `count` 40-byte vertices: position and normal (three floats
 * each, unaligned) and four bone weights. 0x308: the position as in 0049D580 (scale 1); the normal through the
 * bones' rotations times the weights, then normalised (times 1/sqrt of its squared length; VU0's RSQRT of 0
 * gives the largest float). */
#include "gt4_vec.h"
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned char u8;

struct S {
    char p[0x40];
    u32 command;
    char p44[0x1F8 - 0x44];
    u32 *top;
};

extern "C" u8 *func_0049D970(S *s, u8 *cmd);

/* 0x308/0x478's position sum: bones k's rows times pos * scale * w[k], plus its translation times w[k] */
static void skin_position(float *out, const float *pos, const float *w, float scale) {
    float p[3], q[4][3];
    for (int k = 0; k < 3; ++k) p[k] = pos[k] * scale;
    for (int b = 0; b < 4; ++b)
        for (int k = 0; k < 3; ++k) q[b][k] = p[k] * w[b];
    float r[3];
    for (int k = 0; k < 3; ++k) {
        float acc = gt4_vec(8)[k] * q[0][0];
        acc += gt4_vec(9)[k] * q[0][1];
        acc += gt4_vec(10)[k] * q[0][2];
        acc += gt4_vec(11)[k] * w[0];
        acc += gt4_vec(12)[k] * q[1][0];
        acc += gt4_vec(13)[k] * q[1][1];
        acc += gt4_vec(14)[k] * q[1][2];
        acc += gt4_vec(15)[k] * w[1];
        acc += gt4_vec(16)[k] * q[2][0];
        acc += gt4_vec(17)[k] * q[2][1];
        acc += gt4_vec(18)[k] * q[2][2];
        acc += gt4_vec(19)[k] * w[2];
        acc += gt4_vec(20)[k] * q[3][0];
        acc += gt4_vec(21)[k] * q[3][1];
        acc += gt4_vec(22)[k] * q[3][2];
        acc += gt4_vec(23)[k] * w[3];
        r[k] = acc;
    }
    for (int k = 0; k < 3; ++k) out[k] = r[k];
}

/* 0x308's normal: the bones' rotations times nrm * w[k], summed, times 1/sqrt(x*x + y*y + z*z) */
static void skin_normal(float *out, const float *nrm, const float *w) {
    float q[4][3];
    for (int b = 0; b < 4; ++b)
        for (int k = 0; k < 3; ++k) q[b][k] = nrm[k] * w[b];
    float r[3];
    for (int k = 0; k < 3; ++k) {
        float acc = gt4_vec(8)[k] * q[0][0];
        acc += gt4_vec(9)[k] * q[0][1];
        acc += gt4_vec(10)[k] * q[0][2];
        acc += gt4_vec(12)[k] * q[1][0];
        acc += gt4_vec(13)[k] * q[1][1];
        acc += gt4_vec(14)[k] * q[1][2];
        acc += gt4_vec(16)[k] * q[2][0];
        acc += gt4_vec(17)[k] * q[2][1];
        acc += gt4_vec(18)[k] * q[2][2];
        acc += gt4_vec(20)[k] * q[3][0];
        acc += gt4_vec(21)[k] * q[3][1];
        acc += gt4_vec(22)[k] * q[3][2];
        r[k] = acc;
    }
    float len2 = r[0] * r[0];
    len2 += r[1] * r[1];
    len2 += r[2] * r[2];
    const float q2 = gt4_vu_div(1.0f, __gt4_f_sqrt(len2 < 0.0f ? -len2 : len2));
    for (int k = 0; k < 3; ++k) out[k] = r[k] * q2;
}

extern "C" void func_0049D728(S *s, u16 *cmd, int count, int dest) {
    const int bytes = count * 12, nc = count << 16;
    u32 *base = s->top, *w = base;
    w[1] = 0;
    w[2] = nc | 0x6800C000;
    float *a = (float *)(w + 3);
    w = (u32 *)((char *)(w + 3) + bytes);
    w[0] = 0x20000000;
    w[1] = 0xC0C0C0C0;
    w[2] = nc | dest | 0x7800C000;
    float *b = (float *)(w + 3);
    w = (u32 *)((char *)(w + 3) + bytes);
    w[0] = 0x20000000;
    w[1] = s->command;
    w += 2;
    while ((u32)w & 15) *w++ = 0;
    *base = (((u32)((char *)w - (char *)base) >> 4) - 1) | 0x10000000;
    s->top = w;

    u16 tag;
    do {
        tag = cmd[0];
        const u8 *item = func_0049D970(s, (u8 *)(cmd + 2));
        for (int k = tag & 0x7FFF; k != 0; --k) {
            const float *f = (const float *)item;
            skin_position(a, f, f + 6, 1.0f);
            skin_normal(b, f + 3, f + 6);
            a += 3;
            b += 3;
            item += 0x28;
        }
        cmd = (u16 *)item;
    } while (tag & 0x8000);
}
