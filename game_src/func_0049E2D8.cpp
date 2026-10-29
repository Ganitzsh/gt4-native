/* compiler: ee-gcc2.96-nsa-nosib-as2004 */
/* Native version of 0049E2D8 (VU0 asm): a point (three floats, any alignment) to the screen. 0049E3E0 loads the
 * screen matrix into shared vectors 20..23; the point goes through the view matrix (shared vectors 24..27, w = 1),
 * is clamped to shared vector 22, divided by its w (VU0's DIV), scaled by vector 20 and offset by vector 21, and
 * converted to 12.4 fixed point. out: x minus the screen offset (spr +0x26C) / 16, y minus its offset
 * (spr +0x26E) / 8 when the interlace byte (spr +7) is set, else / 16, and the fixed-point z as a float.
 * The decomp's draft is C (0049e2d8_9da0abe236923fa9.c); same symbol. */
#include "gt4_vec.h"
#include "spr.h"
extern "C" void *memcpy(void *, const void *, unsigned int);
typedef unsigned char u8;
typedef unsigned short u16;

struct Spr {
    char pad0[7];
    u8 field;
    char pad8[0x26C - 8];
    u16 offx;
    u16 offy;
};

extern "C" void func_0049E3E0(void);

extern "C" void func_0049E2D8(const float *in, float *out) {
    func_0049E3E0();
    const Spr *spr = (const Spr *)spr_base();
    float p[3], v[4];
    memcpy(p, in, 12);
    gt4_rows4(v, gt4_vec(24), gt4_vec(25), gt4_vec(26), gt4_vec(27), p, 1.0f, 4);
    const float q = gt4_vu_div(1.0f, v[3]);
    int fix[3];
    for (int k = 0; k < 3; ++k) {
        const float c = gt4_minf(v[k], gt4_vec(22)[k]) * q;
        fix[k] = gt4_ftoi(gt4_vec(21)[k] + c * gt4_vec(20)[k], 4);
    }
    const float div = spr->field == 0 ? 16.0f : 8.0f;
    out[0] = (float)(fix[0] - spr->offx) * 0.0625f;
    out[1] = (float)(fix[1] - spr->offy) / div;
    out[2] = (float)fix[2];
}
