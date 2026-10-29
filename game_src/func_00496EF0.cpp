/* compiler: ee-gcc2.96-nsa-nosib-as2004 */
/* Native version of 00496EF0 (unmatched draft): projects a point (three floats, any alignment) to the screen. Loads
 * the screen rows (0049E3E0: shared vectors 20..23), then p = 24..27 times (x, y, z, 1), clamped by 22 and divided by
 * its w; returns p.z (the depth). The screen position 21 + p * 20 in 12.4 fixed point gives *sx = (x - the
 * scratchpad's offset +0x26C) / 16 and *sy = (y - offset +0x26E) / 8, or / 16 when the scratchpad byte +7 is 0, and
 * the scratchpad word +0x248 = (w >> 4) << 24 | (z >> 4). */
#include "gt4_vec.h"
#include "spr.h"
extern "C" void *memcpy(void *, const void *, unsigned int);
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef float f32;

/* Three floats. */
struct Point {
    f32 x, y, z;
};

struct Spr {
    char pad0[7];
    u8 field;
    char pad8[0x248 - 8];
    u32 xyz;
    char pad24C[0x26C - 0x24C];
    u16 offx;
    u16 offy;
};

extern "C" void func_0049E3E0(void);

extern "C" f32 func_00496EF0(Point *in, f32 *sx, f32 *sy) {
    Spr *spr = (Spr *)spr_base();
    f32 div = 8.0f;
    if (spr->field == 0) div = 16.0f;
    func_0049E3E0();
    float v[3], p[4];
    memcpy(v, in, 12);
    gt4_rows4(p, gt4_vec(24), gt4_vec(25), gt4_vec(26), gt4_vec(27), v, 1.0f, 4);
    const float q = gt4_vu_div(1.0f, p[3]);
    const float *lim = gt4_vec(22), *scale = gt4_vec(20), *off = gt4_vec(21);
    int fix[4];
    f32 depth = 0.0f;
    for (int k = 0; k < 4; ++k) {
        const float c = gt4_minf(p[k], lim[k]) * q;
        if (k == 2) depth = c;
        fix[k] = gt4_ftoi(off[k] + c * scale[k], 4);
    }
    *sx = (f32)(fix[0] - (int)spr->offx) * 0.0625f;
    *sy = (f32)(fix[1] - (int)spr->offy) / div;
    spr->xyz = ((u32)(fix[3] >> 4) << 24) | (u32)(fix[2] >> 4);
    return depth;
}
