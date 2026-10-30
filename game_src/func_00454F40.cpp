/* compiler: ee-gcc2.96-nsa-nosib-as2004 */
/* Native version of 00454F40 (matched with VU0 asm): after func_0049E3E0 has set up the view, the w of the point p
 * (three floats, any alignment) through shared vectors 24..27 (w = 1): its depth, at least 0.0001. */
#include "gt4_vec.h"
extern "C" void *memcpy(void *, const void *, unsigned int);

struct Point {
    float x, y, z;
};

extern "C" void func_0049E3E0();

extern "C" float func_00454F40(Point *p) {
    func_0049E3E0();
    float v[3];
    memcpy(v, p, 12);
    const float w = gt4_vec(24)[3] * v[0] + gt4_vec(25)[3] * v[1] + gt4_vec(26)[3] * v[2] + gt4_vec(27)[3];
    return __gt4_maxf(w, 0.0001f);
}
