/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 00499948 (unmatched draft): draws the textured sprite (x0, y0)-(x1, y1) with texture coordinates
 * (u0, v0)-(u1, v1). Its four corners go through VU0 microprogram 0x700 (here in place: shared vectors 16..19 times
 * (x, y, z, 1), the clamp 22, 1 / w, the scale 20 and offset 21; x and y to 12.4 fixed point, z and w to integers),
 * and a GS packet goes to the scratchpad cursor (+0x1F8, advanced by 8 quadwords): DMA cnt tag with VIF FLUSH and
 * DIRECT, a GIF tag with PRIM = flags (+0x4A) | 0x114 and RGBAQ = the colour at +0xC24 with Q = 1.0, then a REGLIST
 * GIF tag and per corner UV = (u * 16 + 0.5, v * 16 + 0.5) truncated, XYZF2 = x, y (16 bits each), z (24 bits),
 * w (8 bits). Natively the sprite is a draw record (compat/game/gt4_gfx.h) with those values instead of the packet.
 * The z lane the original passes the program is the sign extension of x's bits (the upper word of the
 * GPR mfc1 filled): 0, or 0xFFFFFFFF, which the VU reads as -FLT_MAX; it meets row 18 (0 in the sprite path). */
#include "gt4_gfx.h"
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

    /* a strip of four corners: TME, FST, the colour with Q = 1; the first two only start it */
    const u32 flags = (u32)(unsigned short)(s->flags | 0x114);
    const int u[4] = {iu0, iu1, iu0, iu1};
    const int v[4] = {iv0, iv0, iv1, iv1};
    struct gt4_vertex out[4];
    for (int k = 0; k < 4; ++k) {
        struct gt4_vertex *o = &out[k];
        o->x = (unsigned short)c[k][0];
        o->y = (unsigned short)c[k][1];
        gt4_vertex_zf(o, ((u32)c[k][2] & 0xFFFFFF) | ((u32)c[k][3] << 24));
        o->rgba = s->color;
        o->s = o->t = 0.0f;
        o->q = 1.0f;
        o->u = (unsigned short)u[k];
        o->v = (unsigned short)v[k];
        o->kick = k >= 2;
        o->pad = 0;
    }
    gt4_gfx_draw(s, flags, GT4_DRAW_PRIM | GT4_DRAW_FOG | GT4_DRAW_COLOR | GT4_DRAW_UV, out, 4);
}
