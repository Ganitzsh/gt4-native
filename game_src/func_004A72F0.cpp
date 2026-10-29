/* compiler: ee-gcc2.96-as2004 */
/* Native version of 004A72F0 (draft with VU0 asm): the clipping flags of the point p (four floats, any
 * alignment): VCLIPw against |w|, +x -x +y -y +z -z in bits 0..5; when w is negative (its sign bit) each pair of
 * flags is swapped and inverted. The CLIP register (vi18) the original shifts is not state: only the flags of
 * this point reach the result. */
#include "gt4_vec.h"
extern "C" void *memcpy(void *, const void *, unsigned int);

struct Vec4 {
    float x, y, z, w;
};

extern "C" int func_004A72F0(Vec4 *p) {
    float v[4];
    memcpy(v, p, 16);
    const float aw = v[3] < 0.0f ? -v[3] : v[3];
    unsigned int f = 0;
    if (v[0] > aw) f |= 1;
    if (v[0] < -aw) f |= 2;
    if (v[1] > aw) f |= 4;
    if (v[1] < -aw) f |= 8;
    if (v[2] > aw) f |= 16;
    if (v[2] < -aw) f |= 32;
    if (__gt4_float_bits(v[3]) & 0x80000000u)
        f = ((f >> 1) | 0x2A) ^ ((f << 1) | 0x15);
    return (int)(f & 0x3F);
}
