/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 004AC41C (matched with VU0 asm): seeds VU0's random generator: VRINIT sets the R register
 * (__gt4_vu0.r) to 0x3F800000 | the seed's 23 mantissa bits. */
#include "gt4_vec.h"

extern "C" void func_004AC41C(float seed) {
    __gt4_vu0.r = 0x3F800000u | (__gt4_float_bits(seed) & 0x7FFFFFu);
}
