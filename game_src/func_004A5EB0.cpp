/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 004A5EB0 (matched with VU0 asm): sin(a), a in degrees: the helper 004A7438 leaves sin a in
 * shared vector 1's x and cos a in its y. */
#include "gt4_vec.h"
extern "C" void func_004A7438(float);

extern "C" float func_004A5EB0(float farg0) {
    func_004A7438(farg0);
    return gt4_vec(1)[0];
}
