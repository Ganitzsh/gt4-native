/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0049E430 (recovered matched source with VU0 asm): loads the view matrix, shared vectors 20..23,
 * from the current matrix set: the current matrix (vectors 28..31) when the scratchpad's set 0 is current (spr +1 is
 * 0), else set 0's saved copy (spr +0xED0). */
#include "gt4_vec.h"
#include "spr.h"
extern "C" void *memcpy(void *, const void *, unsigned int);
typedef unsigned char u8;

struct Scratch {
    u8 dirty;
    u8 current;
    char pad2[0xED0 - 2];
    float matrix[3][4][4];
};

extern "C" void func_0049E430(void) {
    Scratch *s = (Scratch *)spr_base();
    if (s->current == 0) {
        for (int r = 0; r < 4; ++r) memcpy(gt4_vec(20 + r), gt4_vec(28 + r), 16);
    } else {
        for (int r = 0; r < 4; ++r) memcpy(gt4_vec(20 + r), s->matrix[0][r], 16);
    }
}
