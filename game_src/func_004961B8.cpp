/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 004961B8 (unmatched): projects the point (x, y, z) through the matrix in shared vectors 24..27
 * (w = 1) and clip-tests it against |w| (as 0049B2C8); clipped: 0. Else the screen point a = 21 + (min(p, 22) / w) *
 * 20 and a 16-quadword GS packet (func_004A5B90): a GIF tag, then six (UV, XYZF2) pairs of a triangle strip: XYZF2
 * corners around a sized by `size` (half-widths 0.58 and 0.5 times size, clamped below by the scratchpad viewport at
 * 0x70002FA0/0x70002FB0), UV the same corners of a - viewport (half-widths 0.58 and 0.45, clamped at 0); returns 1.
 * The last half-width is 0.58 * size * 0x3FCCCCCC (the draft had 1.6f, 0x3FCCCCCD; the original loads 0x3FCCCCCC,
 * lui 0x3FCC at 0x496438 / ori 0xCCCC, one ulp lower).
 * Every quadword is the four lanes of a VU register, ftoi4, so the z and w lanes of the offset vector the original
 * builds with mfc1 and pextlw are reproduced: the sign extension of a float's bits, 0 or 0xFFFFFFFF (which the VU
 * reads as -FLT_MAX); for the last pair the upper half of the GPR the previous pextlw left (mfc1 writes the low 64
 * bits). So a negated half-height puts 0x80000000 or 0x7FFFFFFF in a corner's w lane. */
#include "gt4_vec.h"
#include "spr.h"
typedef unsigned int u32;

extern "C" u32 *func_004A5B90(int);

static inline u32 fbits(float f) {
    union {
        float f;
        u32 u;
    } v;
    v.f = f;
    return v.u;
}
static inline float bitsf(u32 u) {
    union {
        float f;
        u32 u;
    } v;
    v.u = u;
    return v.f;
}
/* the VU's value of the sign extension of f's bits: 0 or 0xFFFFFFFF (-FLT_MAX) */
static inline float sign_word(float f) { return (fbits(f) & 0x80000000u) ? -3.40282347e+38f : 0.0f; }

/* a quadword of four ftoi4 lanes */
static inline void put4(u32 *packet, int offset, const float *v) {
    u32 *d = packet + offset / 4;
    for (int k = 0; k < 4; ++k) d[k] = (u32)gt4_ftoi(v[k], 4);
}
static inline void add4(float *d, const float *a, const float *b) {
    for (int k = 0; k < 4; ++k) d[k] = a[k] + b[k];
}
static inline void sub4(float *d, const float *a, const float *b) {
    for (int k = 0; k < 4; ++k) d[k] = a[k] - b[k];
}
static inline void set4(float *d, float x, float y, float z, float w) {
    d[0] = x;
    d[1] = y;
    d[2] = z;
    d[3] = w;
}

extern "C" int func_004961B8(float x, float y, float z, float size) {
    float v[3], a[4];
    v[0] = x;
    v[1] = y;
    v[2] = z;
    gt4_rows4(a, gt4_vec(24), gt4_vec(25), gt4_vec(26), gt4_vec(27), v, 1.0f, 4);
    const int wneg = (fbits(a[3]) & 0x80000000u) != 0;
    const float aw = wneg ? -a[3] : a[3];
    u32 clip = 0;
    if (a[0] > aw) clip |= 1;
    if (a[0] < -aw) clip |= 2;
    if (a[1] > aw) clip |= 4;
    if (a[1] < -aw) clip |= 8;
    if (a[2] > aw) clip |= 16;
    if (a[2] < -aw) clip |= 32;
    if (wneg) clip = ((clip >> 1) | 0x2A) ^ ((clip << 1) | 0x15);
    if (clip & 0x3F) return 0;

    const float *view = (const float *)((char *)spr_base() + 0xFA0); /* vf3: viewport origin */
    const float *size4 = view + 4;                                   /* vf4: viewport size */
    {
        const float q = gt4_vu_div(1.0f, a[3]);
        const float *lim = gt4_vec(22), *scale = gt4_vec(20), *off = gt4_vec(21);
        for (int k = 0; k < 4; ++k) a[k] = off[k] + (gt4_minf(a[k], lim[k]) * q) * scale[k];
    }
    const float o3[4] = {view[0], view[1], view[2], view[3]};
    const float o4[4] = {size4[0], size4[1], size4[2], size4[3]};

    u32 *p = func_004A5B90(16);
    p[0] = 2;
    p[1] = 0x10000000;
    p[2] = 0xEE;
    p[12] = 0x8006;
    p[13] = 0x20AA4000;
    p[14] = 0x43;
    p[6] = 0x3F;
    p[8] = 0x80808080;
    p[10] = 1;
    p[3] = 0;
    p[15] = 0;
    p[4] = 1;
    p[5] = 0;
    p[7] = 0;
    p[9] = 0;
    p[11] = 0;

    const float lox = o4[0] - o3[0], loy = o4[1] + o3[1]; /* vf2.x, vf2.y */
    float d[4], t[4];

    /* positions (XYZF2) */
    const float dx = size * bitsf(0x3F147AE1u); /* 0.58 */
    float dy = size * 0.5f;
    set4(d, dx, dy, sign_word(dx), sign_word(dy));
    add4(t, a, d);
    put4(p, 0x50, t);
    sub4(t, a, d);
    t[0] = gt4_maxf(t[0], lox);
    t[1] = gt4_maxf(t[1], loy);
    put4(p, 0xF0, t);
    dy = -dy;
    set4(d, dx, dy, sign_word(dx), sign_word(dy));
    add4(t, a, d);
    t[1] = gt4_maxf(t[1], lox);
    put4(p, 0xD0, t);
    sub4(t, a, d);
    t[0] = gt4_maxf(t[0], lox);
    put4(p, 0x70, t);
    const float dx2 = dx + dx;
    set4(d, dx2, 0.0f, sign_word(dx2), 0.0f);
    add4(t, a, d);
    put4(p, 0x90, t);
    sub4(t, a, d);
    t[0] = gt4_maxf(t[0], lox);
    put4(p, 0xB0, t);

    /* texture coordinates (UV): the point relative to the viewport */
    sub4(a, a, o4);
    a[0] = a[0] + o3[0];
    a[1] = a[1] - o3[1];
    const float ex = size * bitsf(0x3F147AE1u); /* 0.58 */
    float ey = size * bitsf(0x3EE66666u);       /* 0.45 */
    set4(d, ex, ey, sign_word(ex), sign_word(ey));
    add4(t, a, d);
    put4(p, 0x40, t);
    sub4(t, a, d);
    t[0] = gt4_maxf(t[0], 0.0f);
    t[1] = gt4_maxf(t[1], 0.0f);
    put4(p, 0xE0, t);
    ey = -ey;
    set4(d, ex, ey, sign_word(ex), sign_word(ey));
    add4(t, a, d);
    t[1] = gt4_maxf(t[1], 0.0f);
    put4(p, 0xC0, t);
    sub4(t, a, d);
    t[0] = gt4_maxf(t[0], 0.0f);
    put4(p, 0x60, t);
    const float ex2 = ex * bitsf(0x3FCCCCCCu);
    set4(d, ex2, sign_word(ex2), sign_word(ex), sign_word(ey)); /* z, w: the previous pextlw's upper half */
    add4(t, a, d);
    put4(p, 0x80, t);
    sub4(t, a, d);
    t[0] = gt4_maxf(t[0], 0.0f);
    put4(p, 0xA0, t);
    return 1;
}
