/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 004A5F00 (matched with VU0 asm): *s = sin(angle), *c = cos(angle), angle in degrees, and
 * returns the sine: the helper 004A7438 leaves sin in shared vector 1's x and cos in its y. C++ linkage, as the
 * matched source declares it (and its call of 004A7438). */
#include "gt4_vec.h"

extern void func_004A7438(float angle);

extern float func_004A5F00(float *s, float *c, float angle)
{
    func_004A7438(angle);
    const float sine = gt4_vec(1)[0];
    const float cosine = gt4_vec(1)[1];
    *s = sine;
    *c = cosine;
    return sine;
}
