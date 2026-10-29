/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 004ABD40 (draft with VU0 asm): sets a lighting parameter of the scratchpad state when bit 0
 * of flag is set. which 11: the level, value (unsigned) clamped to [0, 128], and flag bit 2 of the scratchpad's
 * flags. Else: value's four bytes as floats in [0, 1] (each times 1/255) go to 004ABCE0 (which stores them by
 * `which` and sets the same flag). */
#include "gt4_vec.h"
#include "spr.h"
extern "C" void *memcpy(void *, const void *, unsigned int);
typedef float f32;
typedef unsigned int u32;
typedef unsigned int u128 __attribute__((mode(TI)));

struct Spr {
    char pad0[0x48];
    unsigned short flags;
    char pad4A[0xAA0 - 0x4A];
    f32 level;
};

extern "C" void func_004ABCE0(Spr *m, int unused, unsigned int which, u128 v) throw();

extern "C" void func_004ABD40(int flag, unsigned int which, u32 value) {
    Spr *p = (Spr *)spr_base();
    if (!(flag & 1))
        return;
    if (which == 11) {
        p->flags |= 4;
        p->level = __gt4_minf(__gt4_maxf((f32)value, 0.0f), 128.0f);
        return;
    }
    const float k = __gt4_bits_float(0x3B808081u);   /* 1/255 */
    float c[4];
    for (int i = 0; i < 4; ++i)
        c[i] = (float)(int)((value >> (8 * i)) & 0xFF) * k;
    u128 r;
    memcpy(&r, c, 16);
    func_004ABCE0(p, 1, which, r);
}
