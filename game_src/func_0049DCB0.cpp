/* compiler: ee-gcc2.96-nsa-nosib-as2004 */
/* Native version of 0049DCB0 (VU0 asm, VU0 microprogram 0x2E8): a batch of packed vertices (position and normal)
 * skinned by four bones into a VIF packet at s->top (+0x1F8), laid out as 0049D728's (positions, then normals
 * unpacked to `dest`, then s->command). The command stream: groups of a header byte (count in bits 0..6, bit 7:
 * another group follows), a byte the bone loader ignores, four bone index bytes (0049D970 loads the bones into
 * shared vectors 8..23), then `count` 13-byte vertices: x, y, z as 16-bit integers (bytes 0..5), the normal as
 * three signed bytes (6..8) and four signed-byte bone weights (9..12). 0x2E8 converts them to floats (the weights
 * / 127), then 0x308 skins the position (times `scale`) and the normal, which it normalises. */
#include "gt4_vec.h"
typedef unsigned int u32;
typedef unsigned char u8;
typedef short s16;
typedef signed char s8;

struct S {
    char p[0x40];
    u32 command;
    char p44[0x1F8 - 0x44];
    u32 *top;
};

extern "C" u8 *func_0049D970(S *s, u8 *cmd);

/* 0x308's position sum: bones k's rows times pos * scale * w[k], plus its translation times w[k] */
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

/* 1/127 as the microprograms' I register holds it (0x3C010204) */
static float inv127() {
    union {
        u32 u;
        float f;
    } c;
    c.u = 0x3C010204u;
    return c.f;
}

extern "C" void func_0049DCB0(S *s, unsigned char *p, int count, int dest, float scale) {
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

    const float i127 = inv127();
    unsigned int tag;
    do {
        tag = p[0];
        const u8 *item = func_0049D970(s, p + 2);
        for (int k = tag & 0x7F; k != 0; --k) {
            float pos[3], nrm[3], wt[4];
            for (int j = 0; j < 3; ++j) pos[j] = (float)(s16)(item[2 * j] | item[2 * j + 1] << 8);
            for (int j = 0; j < 3; ++j) nrm[j] = (float)(s8)item[6 + j];
            for (int j = 0; j < 4; ++j) wt[j] = (float)(s8)item[9 + j] * i127;
            skin_position(a, pos, wt, scale);
            skin_normal(b, nrm, wt);
            a += 3;
            b += 3;
            item += 13;
        }
        p = (unsigned char *)item;
    } while (tag & 0x80);
}
