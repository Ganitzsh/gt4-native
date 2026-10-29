/* compiler: ee-gcc2.96-as2004 */
/* Native version of 004998A8 (unmatched draft): sets up the sprite projection. Updates the view first if the
 * scratchpad's flag says so (0049E480); shared vectors 20..23 = the scratchpad's screen rows (+0xFA0); then from the
 * x, y of three points v[0..2] (three floats each, any alignment) and shared vectors 24, 25, 27: 16 = 24 * v0.x +
 * 25 * v0.y, 17 the same for v1, 18 = 0, 19 = 24 * v2.x + 25 * v2.y + 27 (four lanes each). */
#include "gt4_vec.h"
#include "spr.h"
extern "C" void *memcpy(void *, const void *, unsigned int);
typedef float f32;
typedef unsigned char u8;

struct Scratch {
    u8 dirty;
    char pad1[0xFA0 - 1];
    f32 m[4][4];
};

struct V3 {
    f32 x, y, z;
};

extern "C" void func_0049E480(Scratch *s);

extern "C" void func_004998A8(V3 *v) {
    Scratch *s = (Scratch *)spr_base();
    if (s->dirty) func_0049E480(s);
    float a[2], b[2], c[2];
    memcpy(a, &v[0], 8);
    memcpy(b, &v[1], 8);
    memcpy(c, &v[2], 8);
    for (int r = 0; r < 4; ++r) memcpy(gt4_vec(20 + r), s->m[r], 16);
    const float *r0 = gt4_vec(24), *r1 = gt4_vec(25), *r3 = gt4_vec(27);
    float *v16 = gt4_vec(16), *v17 = gt4_vec(17), *v18 = gt4_vec(18), *v19 = gt4_vec(19);
    for (int k = 0; k < 4; ++k) {
        v16[k] = r0[k] * a[0] + r1[k] * a[1];
        v17[k] = r0[k] * b[0] + r1[k] * b[1];
        v18[k] = 0.0f;
        v19[k] = r0[k] * c[0] + r1[k] * c[1] + r3[k];
    }
}
