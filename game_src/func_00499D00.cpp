/* compiler: ee-gcc2.96-nogcse */
/* Native version of 00499D00 (unmatched draft): draws the untextured rectangle (x, y)-(x + w, y + h) through the
 * sprite projection m (three points, three floats each, any alignment). After 0049FE28 and the view update when the
 * scratchpad's flag says so (0049E480), it sets shared vectors 20..23 to the scratchpad's screen rows (+0xFA0),
 * 16 = 24 * m0.x + 25 * m0.y, 17 the same for m1, 18 = 0, 19 = 24 * m2.x + 25 * m2.y + 27; the four corners go
 * through VU0 microprogram 0x700 (here in place: 16..19 times (x, y, z, 1), the clamp 22, 1 / w, the scale 20 and
 * offset 21; x and y to 12.4 fixed point, z and w to integers), and a GS packet goes to the scratchpad cursor
 * (+0x1F8, advanced by 5 quadwords): DMA cnt tag with VIF FLUSH and DIRECT, a GIF tag with PRIM = (flags (+0x4A) | 4)
 * without bit 4 and RGBAQ = the colour at +0xC20 with Q = 1.0, then XYZF2 per corner: x, y (16 bits each), z
 * (24 bits), w (8 bits). The z lane the original passes the program is the sign extension of x's bits (0, or
 * 0xFFFFFFFF, which the VU reads as -FLT_MAX); row 18 is 0, so it adds nothing. Natively the corners are a draw
 * record (compat/game/gt4_gfx.h) with those values instead of the packet: the first two only start the strip. */
#include "gt4_gfx.h"
#include "gt4_vec.h"
#include "spr.h"
extern "C" void *memcpy(void *, const void *, unsigned int);
typedef unsigned int u32;
typedef float f32;

struct Scratch {
    unsigned char dirty;
    char pad[0x49];
    unsigned short flags;
    char pad4c[0x1F8 - 0x4C];
    char *cursor;
    char pad1fc[0xC20 - 0x1FC];
    u32 color;
    char padc24[0xFA0 - 0xC24];
    f32 matrix[4][4];
};

extern "C" void func_0049FE28();
extern "C" void func_0049E480(void *);

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


extern "C" void func_00499D00(char *m, float x, float y, float w, float h) {
    Scratch *s = (Scratch *)spr_base();
    func_0049FE28();
    if (s->dirty) func_0049E480(s);
    const float x2 = x + w, y2 = y + h;
    float a[2], b[2], c[2];
    memcpy(a, m, 8);
    memcpy(b, m + 12, 8);
    memcpy(c, m + 24, 8);
    for (int r = 0; r < 4; ++r) memcpy(gt4_vec(20 + r), s->matrix[r], 16);
    {
        const float *r0 = gt4_vec(24), *r1 = gt4_vec(25), *r3 = gt4_vec(27);
        float *v16 = gt4_vec(16), *v17 = gt4_vec(17), *v18 = gt4_vec(18), *v19 = gt4_vec(19);
        for (int k = 0; k < 4; ++k) {
            v16[k] = r0[k] * a[0] + r1[k] * a[1];
            v17[k] = r0[k] * b[0] + r1[k] * b[1];
            v18[k] = 0.0f;
            v19[k] = r0[k] * c[0] + r1[k] * c[1] + r3[k];
        }
    }
    int pt[4][4];
    screen_point(pt[0], x, y, sign_word(x));
    screen_point(pt[1], x2, y, sign_word(x2));
    screen_point(pt[2], x, y2, sign_word(x));
    screen_point(pt[3], x2, y2, sign_word(x2));

    const u32 flags = ((u32)s->flags | 4) & 0xFFEF;
    struct gt4_vertex out[4];
    for (int k = 0; k < 4; ++k) {
        struct gt4_vertex *v = &out[k];
        v->x = (unsigned short)pt[k][0];
        v->y = (unsigned short)pt[k][1];
        gt4_vertex_zf(v, ((u32)pt[k][2] & 0xFFFFFF) | ((u32)pt[k][3] << 24));
        v->rgba = s->color;
        v->s = v->t = 0.0f;
        v->q = 1.0f;
        v->u = v->v = 0;
        v->kick = k >= 2;
        v->pad = 0;
    }
    gt4_gfx_draw(s, flags, GT4_DRAW_PRIM | GT4_DRAW_FOG | GT4_DRAW_COLOR, out, 4);
}
