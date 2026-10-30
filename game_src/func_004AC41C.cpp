/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 004AC41C (matched with VU0 asm): seeds the game's random generator (gt4_random_state, the
 * original's VU0 R register, which VRINIT sets): 0x3F800000 | the seed's 23 mantissa bits. */
#include "gt4_vec.h"

extern "C" void func_004AC41C(float seed) {
    gt4_random_state = 0x3F800000u | (__gt4_float_bits(seed) & 0x7FFFFFu);
}
