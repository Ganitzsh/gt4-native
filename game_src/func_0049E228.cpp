/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0049E228 (recovered matched source with VU0 asm): stores the matrix of src in the scratchpad's
 * matrix (spr +0xA60), its four rows multiplied lane by lane by the global scale rows (D_00624CC0 + 0x20) when the
 * scaling is enabled (D_00624CC0 + 0x60) and src asks for it (+0x44 bit 0); copies src's float (+0x40) to spr +0xAA0
 * and sets spr's flags (+0x48): bit 0x80 from src +0x44 bit 1, bit 4 always. When it scales, the original leaves the
 * last scale row in VU0 vf8, a shared vector (8..19: bones and constants): kept so. */
#include "gt4_vec.h"
#include "spr.h"
extern "C" void *memcpy(void *, const void *, unsigned int);
typedef float f32;
typedef unsigned char u8;

struct Src {
    f32 m[4][4];
    f32 f40;
    u8 f44;
};

struct Glob {
    char pad0[0x20];
    f32 scale[4][4];
    u8 enable;
};

struct Spr {
    char pad0[0x48];
    unsigned short flags;
    char pad4A[0xA60 - 0x4A];
    f32 m[4][4];
    f32 fAA0;
};

extern Glob D_00624CC0;

extern "C" void func_0049E228(Src *src) {
    Spr *p = (Spr *)spr_base();
    Glob *g = &D_00624CC0;
    f32 rows[4][4];
    memcpy(rows, src->m, 64);
    if (g->enable && (u8)(src->f44 & 1)) {
        for (int r = 0; r < 4; ++r)
            for (int k = 0; k < 4; ++k) rows[r][k] = rows[r][k] * g->scale[r][k];
        memcpy(gt4_vec(8), g->scale[3], 16);
    }
    p->fAA0 = src->f40;
    if (src->f44 & 2)
        p->flags |= 0x80;
    else
        p->flags &= ~0x80;
    p->flags |= 4;
    memcpy(p->m, rows, 64);
}
