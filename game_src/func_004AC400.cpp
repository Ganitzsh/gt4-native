/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 004AC400 (draft with VU0 asm): the next value of VU0's random generator, in [0, 1). VRNEXT
 * steps the R register (__gt4_vu0.r: 0x3F800000 | 23 bits, a float in [1, 2); the shift and feedback as the
 * runtime's VRNEXT) and the result is that float minus 1. */
#include "gt4_vec.h"

extern "C" float func_004AC400(void) {
    const unsigned int x = __gt4_vu0.r;
    const unsigned int bit = ((x >> 4) ^ (x >> 22)) & 1;
    __gt4_vu0.r = 0x3F800000u | (((x << 1) | bit) & 0x7FFFFFu);
    return __gt4_bits_float(__gt4_vu0.r) - 1.0f;
}
