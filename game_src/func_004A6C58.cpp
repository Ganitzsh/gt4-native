/* compiler: ee-gcc2.96-nsa-nosib-as2004 */
/* Native version of 004A6C58 (swarm draft with VU0 asm): a line strip of n >= 2 points (x, y pairs, any alignment)
 * as one GS packet at the scratchpad's packet cursor: DMA tag and VIF codes, a GIF tag that sets RGBAQ (the
 * scratchpad colour, Q = 1) and PRIM (the scratchpad's primitive bits, line strip), then n packed XYZ2 records.
 * Each point goes through rows 24, 25 and 27 (shared vectors: x, y, 1), is divided by its w (after a clamp by
 * the screen row loaded into shared vector 22), scaled and offset by the screen rows 20, 21 and converted to
 * 12.4 fixed point; its w lane gets 2048 (from shared vector 9) when this point and the previous one are outside
 * the same clipping plane. Shared vectors 20..23 are left holding the scratchpad's screen rows (0x70002FA0) and
 * 9 = (0, 0, 0, 2048), as the original leaves them. Natively the points are a draw record (compat/game/gt4_gfx.h)
 * with the same values: a point whose w got 2048 (its ADC bit, bit 15 of the fourth word) does not draw. */
#include "gt4_gfx.h"
#include "gt4_vec.h"
#include "spr.h"
extern "C" void *memcpy(void *, const void *, unsigned int);
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

struct XY {
    float x, y;
};

extern "C" void func_0049E480(void *s);

/* VCLIPw of p against |p.w| (+x -x +y -y +z -z in bits 0..5), and the flags 004A72F0 makes of it: when w is
 * negative (its sign bit) each pair of flags is swapped and inverted. */
static inline u32 clip_flags(const float *p) {
    const float aw = p[3] < 0.0f ? -p[3] : p[3];
    u32 f = 0;
    if (p[0] > aw) f |= 1;
    if (p[0] < -aw) f |= 2;
    if (p[1] > aw) f |= 4;
    if (p[1] < -aw) f |= 8;
    if (p[2] > aw) f |= 16;
    if (p[2] < -aw) f |= 32;
    if (__gt4_float_bits(p[3]) & 0x80000000u)
        f = ((f >> 1) | 0x2A) ^ ((f << 1) | 0x15);
    return f & 0x3F;
}

extern "C" void func_004A6C58(XY *pts, int n) {
    char *s = (char *)spr_base();
    if (n < 2)
        return;
    if (*(u8 *)(s + 0x0))
        func_0049E480(s);
    const u32 prim = (u32)((*(u16 *)(s + 0x4A) & 0xFFEF) | 2);
    const int count = n;
    struct gt4_vertex *verts = (struct gt4_vertex *)__builtin_alloca(n * sizeof(struct gt4_vertex));
    struct gt4_vertex *vo = verts;
    int out[4];

    for (int r = 0; r < 4; ++r)
        memcpy(gt4_vec(20 + r), s + 0xFA0 + 0x10 * r, 16);
    float *vf9 = gt4_vec(9);
    vf9[0] = 0.0f;
    vf9[1] = 0.0f;
    vf9[2] = 0.0f;
    vf9[3] = __gt4_bits_float(0x45000000u);   /* 2048 */
    const float *m0 = gt4_vec(24), *m1 = gt4_vec(25), *m3 = gt4_vec(27);
    const float *scale = gt4_vec(20), *offset = gt4_vec(21), *limit = gt4_vec(22);

    u32 prev = 0;
    do {
        XY pt;
        memcpy(&pt, pts, 8);
        pts++;
        float v[4], q[4];
        for (int k = 0; k < 4; ++k)
            v[k] = m0[k] * pt.x + m1[k] * pt.y + m3[k];
        const float invw = gt4_vu_div(1.0f, v[3]);
        for (int k = 0; k < 4; ++k)
            q[k] = __gt4_minf(v[k], limit[k]);
        const u32 flag = clip_flags(v);
        for (int k = 0; k < 4; ++k) {
            q[k] = q[k] * invw;
            q[k] = offset[k] + q[k] * scale[k];
        }
        if (prev & flag)
            for (int k = 0; k < 4; ++k)
                q[k] = q[k] + vf9[k];
        for (int k = 0; k < 4; ++k)
            out[k] = gt4_ftoi(q[k], 4);
        prev = flag;
        vo->x = (unsigned short)out[0];
        vo->y = (unsigned short)out[1];
        vo->z = (u32)out[2];
        vo->fog = 0;
        vo->rgba = *(u32 *)(s + 0xC20);
        vo->s = vo->t = 0.0f;
        vo->q = 1.0f;
        vo->u = vo->v = 0;
        vo->kick = !(((u32)out[3] >> 15) & 1);
        vo->pad = 0;
        vo++;
    } while (--n != 0);
    gt4_gfx_draw(s, prim, GT4_DRAW_PRIM | GT4_DRAW_COLOR, verts, count);
}
