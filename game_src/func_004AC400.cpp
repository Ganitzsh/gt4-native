/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 004AC400 (draft with VU0 asm): the next value of the game's random generator, in [0, 1). The
 * state (gt4_random_state: 0x3F800000 | 23 bits, a float in [1, 2); the original's VU0 R register) steps as VRNEXT
 * steps it (shifted left by one, the new low bit bits 4 xor 22), and the result is that float minus 1. */
#include "gt4_vec.h"

extern "C" float func_004AC400(void) {
    const unsigned int x = gt4_random_state;
    const unsigned int bit = ((x >> 4) ^ (x >> 22)) & 1;
    gt4_random_state = 0x3F800000u | (((x << 1) | bit) & 0x7FFFFFu);
    return __gt4_bits_float(gt4_random_state) - 1.0f;
}
