/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 004AC118 (draft with VU0 asm): atan2(y, x) in radians. On an axis: 0, pi, pi/2 or -pi/2
 * (0x40490FDA, 0x3FC90FDA: one ulp under). Else the arctangent of the smaller of |x|, |y| over the larger, as VU0
 * microprogram 0x548 computes it (atan((s - b) / (s + b)) + pi/4, an odd polynomial in t = (s - b) / (s + b)),
 * negated and offset by the octant's entries of the tables at 006B0018 (sign) and 006B0038 (offset). The
 * program leaves shared vector 8 = (1, 1, 1, 2), as here. */
#include "gt4_vec.h"
typedef float f32;

extern f32 D_006B0018[8];
extern f32 D_006B0038[8];

/* VU0 microprogram 0x548, lane x: atan(small / big) for 0 < small <= big */
static float atan_ratio(float big, float small) {
    const float t = gt4_vu_div(small - big, small + big);
    const float t2 = t * t;
    float acc = t * __gt4_bits_float(0x3F7FFFF5u);
    float p = t2 * t;                                  /* t^3 */
    acc = acc + p * __gt4_bits_float(0xBEAAA61Cu);
    p = p * t2;                                        /* t^5 */
    acc = acc + p * __gt4_bits_float(0x3E4C40A6u);
    p = p * t2;                                        /* t^7 */
    acc = acc + p * __gt4_bits_float(0xBE0E6C63u);
    p = p * t2;                                        /* t^9 */
    acc = acc + p * __gt4_bits_float(0x3DC577DFu);
    p = p * t2;                                        /* t^11 */
    acc = acc + p * __gt4_bits_float(0xBD6501C4u);
    p = p * t2;                                        /* t^13 */
    acc = acc + p * __gt4_bits_float(0x3CB31652u);
    p = p * t2;                                        /* t^15 */
    acc = acc + __gt4_bits_float(0x3F490FDBu);         /* pi/4 (vf8.x = 1 times I) */
    return acc + p * __gt4_bits_float(0xBB84D7E7u);
}

extern "C" f32 func_004AC118(f32 y, f32 x) {
    if (y == 0.0f) {
        if (0.0f <= x)
            return 0.0f;
        return __gt4_bits_float(0x40490FDAu);
    }
    if (x == 0.0f) {
        if (0.0f <= y)
            return __gt4_bits_float(0x3FC90FDAu);
        return __gt4_bits_float(0xBFC90FDAu);
    }
    const f32 ax = x < 0.0f ? -x : x;
    const f32 ay = y < 0.0f ? -y : y;
    f32 r = ay <= ax ? atan_ratio(ax, ay) : atan_ratio(ay, ax);
    float *vf8 = gt4_vec(8);
    vf8[0] = vf8[1] = vf8[2] = 1.0f;
    vf8[3] = 2.0f;
    unsigned int oct = 0;
    if (y < 0.0f)
        oct = 4;
    if (x < 0.0f)
        oct |= 2;
    if (ax < ay)
        oct |= 1;
    const f32 sign = D_006B0018[oct];
    const f32 off = D_006B0038[oct];
    if (!(0.0f <= sign))
        r = -r;
    return r + off;
}
