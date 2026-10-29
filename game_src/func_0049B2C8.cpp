/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0049B2C8 (unmatched draft): the point (x, y, z), three floats passed as their bits, through the
 * matrix in shared vectors 24..27 (w = 1); returns its clip code: the VU's CLIP flags against |w| (bit 0 x > |w|,
 * 1 x < -|w|, 2 and 3 for y, 4 and 5 for z), and when w is negative (its sign bit) each bit inverted and swapped
 * with its pair's. */
#include "gt4_vec.h"
extern "C" void *memcpy(void *, const void *, unsigned int);

extern "C" int func_0049B2C8(int x, int y, int z) {
    /* the one caller (0049B330) loads the bits with LWU, zero-extended: with a sign-extended x the original would
       OR x's upper half into y's lane */
    float v[3], p[4];
    memcpy(&v[0], &x, 4);
    memcpy(&v[1], &y, 4);
    memcpy(&v[2], &z, 4);
    gt4_rows4(p, gt4_vec(24), gt4_vec(25), gt4_vec(26), gt4_vec(27), v, 1.0f, 4);
    int wbits;
    memcpy(&wbits, &p[3], 4);
    const float aw = wbits < 0 ? -p[3] : p[3];
    unsigned int clip = 0;
    if (p[0] > aw) clip |= 1;
    if (p[0] < -aw) clip |= 2;
    if (p[1] > aw) clip |= 4;
    if (p[1] < -aw) clip |= 8;
    if (p[2] > aw) clip |= 16;
    if (p[2] < -aw) clip |= 32;
    if (wbits < 0) clip = ((clip >> 1) | 0x2A) ^ ((clip << 1) | 0x15);
    return (int)(clip & 0x3F);
}
