/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 00499948 (unmatched draft): draws the textured sprite (x0, y0)-(x1, y1) with texture coordinates
 * (u0, v0)-(u1, v1). Its four corners go through VU0 microprogram 0x700 (here in place: shared vectors 16..19 times
 * (x, y, z, 1), the clamp 22, 1 / w, the scale 20 and offset 21; x and y to 12.4 fixed point, z and w to integers),
 * and a GS packet goes to the scratchpad cursor (+0x1F8, advanced by 8 quadwords): DMA cnt tag with VIF FLUSH and
 * DIRECT, a GIF tag with PRIM = flags (+0x4A) | 0x114 and RGBAQ = the colour at +0xC24 with Q = 1.0, then a REGLIST
 * GIF tag and per corner UV = (u * 16 + 0.5, v * 16 + 0.5) truncated, XYZF2 = x, y (16 bits each), z (24 bits),
 * w (8 bits). The z lane the original passes the program is the sign extension of x's bits (the upper word of the
 * GPR mfc1 filled): 0, or 0xFFFFFFFF, which the VU reads as -FLT_MAX; it meets row 18 (0 in the sprite path). */
#include "gt4_vec.h"
#include "spr.h"
typedef unsigned int u32;
typedef float f32;

struct Spr {
    char p0[0x4A];
    unsigned short flags;
    char p1[0x1F8 - 0x4C];
    char *cursor;
    char p2[0xC24 - 0x1FC];
    u32 color;
};

static inline u32 fbits(float f) {
    union {
        float f;
        u32 u;
    } v;
    v.f = f;
    return v.u;
}
/* the VU's value of the sign extension of f's bits: 0 or 0xFFFFFFFF (-FLT_MAX) */
static inline float sign_word(float f) { return (fbits(f) & 0x80000000u) ? -3.40282347e+38f : 0.0f; }
static inline float vu_clamp(float f) {
    if (f > 3.40282347e+38f) return 3.40282347e+38f;
    if (f < -3.40282347e+38f) return -3.40282347e+38f;
    return f;
}

/* microprogram 0x700 for one point: out = x, y in 12.4 and z, w as integers */
static void screen_point(int out[4], float x, float y, float z) {
    const float *r0 = gt4_vec(16), *r1 = gt4_vec(17), *r2 = gt4_vec(18), *r3 = gt4_vec(19);
    const float *lim = gt4_vec(22), *scale = gt4_vec(20), *off = gt4_vec(21);
    float p[4];
    for (int k = 0; k < 4; ++k) p[k] = vu_clamp(r0[k] * x + r1[k] * y + vu_clamp(r2[k] * z) + r3[k]);
    const float q = gt4_vu_div(1.0f, p[3]);
    for (int k = 0; k < 4; ++k) {
        const float v = gt4_minf(p[k], lim[k]) * q;
        out[k] = gt4_ftoi(off[k] + v * scale[k], k < 2 ? 4 : 0);
    }
}

/* XYZF2: x, y (16 bits each), z (24 bits), w (8 bits) */
static inline void put_xyzf(u32 *d, const int *s) {
    d[0] = ((u32)s[0] & 0xFFFF) | ((u32)s[1] << 16);
    d[1] = ((u32)s[2] & 0xFFFFFF) | ((u32)s[3] << 24);
}

extern "C" void func_00499948(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1) {
    Spr *s = (Spr *)spr_base();
    int c[4][4];
    screen_point(c[0], x0, y0, sign_word(x0));
    screen_point(c[1], x1, y0, sign_word(x1));
    screen_point(c[2], x0, y1, sign_word(x0));
    screen_point(c[3], x1, y1, sign_word(x1));
    float t;
    t = v1 * 16.0f;
    const int iv1 = gt4_ftoi(t + 0.5f, 0);
    t = u0 * 16.0f;
    const int iu0 = gt4_ftoi(t + 0.5f, 0);
    t = v0 * 16.0f;
    const int iv0 = gt4_ftoi(t + 0.5f, 0);
    t = u1 * 16.0f;
    const int iu1 = gt4_ftoi(t + 0.5f, 0);

    u32 *p = (u32 *)s->cursor;
    const u32 flags = (u32)(unsigned short)(s->flags | 0x114);
    const u32 color = s->color;
    p[0] = 0x10000007;
    p[3] = 0x50000007;
    p[2] = 0x11000000;
    p[1] = 0;
    s->cursor += 0x80;
    p[4] = 1;
    p[5] = 0x24000000;
    p[6] = 0x10;
    p[7] = 0;
    p[8] = flags;
    p[9] = 0;
    p[10] = color;
    p[11] = 0x3F800000;
    p[14] = 0x4343C3C3;
    p[15] = 0;
    p[12] = 0x8001;
    p[13] = 0x84000000;
    const int u[4] = {iu0, iu1, iu0, iu1};
    const int v[4] = {iv0, iv0, iv1, iv1};
    for (int k = 0; k < 4; ++k) {
        u32 *q = p + 16 + 4 * k;
        ((unsigned short *)q)[0] = (unsigned short)u[k];
        ((unsigned short *)q)[1] = (unsigned short)v[k];
        q[1] = 0;
        put_xyzf(q + 2, c[k]);
    }
}
