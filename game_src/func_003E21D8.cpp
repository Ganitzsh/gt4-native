/* compiler: ee-gcc2.96-nsa-nosib-as2004 */
/* Native version of 003E21D8 (matched with VU0 asm): out = the rotation in shared vectors 20..22 applied to in
 * (three floats each, not 16-byte aligned). */
#include "gt4_vec.h"
extern "C" void *memcpy(void *, const void *, unsigned int);
extern "C" void func_003E21D8(float *out, const float *in) {
    float v[3], r[3];
    memcpy(v, in, 12);
    gt4_rows3(r, gt4_vec(20), gt4_vec(21), gt4_vec(22), v, 3);
    memcpy(out, r, 12);
}
