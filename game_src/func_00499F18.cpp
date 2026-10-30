/* compiler: ee-gcc2.96-as2004 */
/* Native version of 00499F18 (recovered matched source with VU0 asm): sets up the 2D projection from three points
 * (x, y of v[0], v[1], v[2]; 12-byte stride). Refreshes the view matrix first when the scratch block is dirty
 * (0049E480). Leaves, as the original does in VU0: shared vectors 20..23 = the screen rows at s + 0xFA0; 16 = row 24 *
 * a.x + row 25 * a.y; 17 the same with b; 18 = 0; 19 = row 24 * c.x + row 25 * c.y + row 27 (all four lanes). */
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
#include "gt4_vec.h"

extern "C" void func_00499F18(Scratch *s, V3 *v) {
    if (s->dirty)
        func_0049E480(s);
    const f32 bx = v[1].x, by = v[1].y, cx = v[2].x, cy = v[2].y, ax = v[0].x, ay = v[0].y;
    for (int r = 0; r < 4; ++r) memcpy(gt4_vec(20 + r), s->m[r], 16);
    const float *r24 = gt4_vec(24), *r25 = gt4_vec(25), *r27 = gt4_vec(27);
    float *v16 = gt4_vec(16), *v17 = gt4_vec(17), *v18 = gt4_vec(18), *v19 = gt4_vec(19);
    for (int k = 0; k < 4; ++k) {
        v16[k] = r24[k] * ax + r25[k] * ay;
        v17[k] = r24[k] * bx + r25[k] * by;
        v18[k] = 0.0f;
        v19[k] = r24[k] * cx + r25[k] * cy + r27[k];
    }
}
