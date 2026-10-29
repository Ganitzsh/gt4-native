/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0049E3E0 (matched with VU0 asm): loads the scratchpad's screen matrix (spr +0xFA0..0xFD0) into
 * shared vectors 20..23, after refreshing the view matrices (0049E480) when the dirty byte (spr +0) is set. */
#include "gt4_vec.h"
#include "spr.h"

struct Scratch {
    unsigned char dirty;
    char pad[0xF9F];
    float matrix[4][4];
};

extern "C" void func_0049E480(Scratch *s);

extern "C" void func_0049E3E0(void) {
    Scratch *s = (Scratch *)spr_base();
    if (s->dirty) func_0049E480(s);
    for (int r = 0; r < 4; ++r) {
        float *v = gt4_vec(20 + r);
        for (int k = 0; k < 4; ++k) v[k] = s->matrix[r][k];
    }
}
