/* Native version of 0048EA48 (draft with VU0 asm; the draft has no compiler line): out = b * a for 4x4 matrices of
 * four 16-byte rows (row k of out is row k of b through the rows of a); out may be a or b. Leaves a's last row in
 * shared vector 8, as the original leaves VU0's registers. */
#include "gt4_vec.h"
typedef unsigned int u128 __attribute__((mode(TI)));

extern "C" void func_0048EA48(u128 *out, u128 *a, u128 *b) {
    const float *pa = (const float *)a, *pb = (const float *)b;
    float r[16], va[16];
    for (int k = 0; k < 16; ++k) va[k] = pa[k];
    for (int row = 0; row < 4; ++row)
        gt4_rows4(r + 4 * row, va, va + 4, va + 8, va + 12, pb + 4 * row, pb[4 * row + 3], 4);
    float *po = (float *)out;
    for (int k = 0; k < 16; ++k) po[k] = r[k];
    float *v8 = gt4_vec(8);
    for (int k = 0; k < 4; ++k) v8[k] = va[12 + k];
}
