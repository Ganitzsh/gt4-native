/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 004A53F8 (matched with VU0 asm): pushes the current matrix (shared vectors 28..31) on the
 * matrix stack in VU0 data memory (0x11004000, quadword vi13, wrapping at 256), as VU0 microprogram 0x900 does:
 * row 28 first, vi13 one up per row. */
#include "gt4_vec.h"
extern "C" void *memcpy(void *, const void *, unsigned int);

extern "C" void func_004A53F8(void) {
    unsigned int vi = __gt4_vu0.vi[13];
    for (int r = 28; r <= 31; ++r) {
        memcpy((void *)(0x11004000u + ((vi & 0xFF) << 4)), gt4_vec(r), 16);
        vi = (vi + 1) & 0xFFFF;
    }
    __gt4_vu0.vi[13] = vi;
}
