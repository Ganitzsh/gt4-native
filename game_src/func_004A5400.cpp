/* Native version of 004A5400 (matched with VU0 asm): marks the matrix changed (scratchpad byte 0x70002000) and
 * pops the current matrix (shared vectors 28..31) from the matrix stack in VU0 data memory (0x11004000, quadword
 * vi13, wrapping at 256), as VU0 microprogram 0x930 does: vi13 one down per row, row 31 first. */
#include "gt4_vec.h"
#include "spr.h"
extern "C" void *memcpy(void *, const void *, unsigned int);

struct Spr {
    unsigned char dirty;
};

extern "C" void func_004A5400(void) {
    Spr *s = (Spr *)spr_base();
    s->dirty = 1;
    unsigned int vi = __gt4_vu0.vi[13];
    for (int r = 31; r >= 28; --r) {
        vi = (vi - 1) & 0xFFFF;
        memcpy(gt4_vec(r), (const void *)(0x11004000u + ((vi & 0xFF) << 4)), 16);
    }
    __gt4_vu0.vi[13] = vi;
}
