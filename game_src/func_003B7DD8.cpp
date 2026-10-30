/* compiler: ee-gcc2.96-nsa-nosib-as2004 */
/* Native version of 003B7DD8 (provisional draft with VU0 asm): v (three floats, any alignment) through the rotation
 * in shared vectors 28..30, its dot product with dir divided by dir's length (rsqrt.s), raised to the power `power`
 * rounded (at most 32); -1 when that cosine is negative, 1 when the power rounds to 0. */
#include "gt4_vec.h"
typedef float f32;

struct Vec3 {
    f32 x, y, z;
};

/* the EE FPU's rsqrt.s: a / sqrt(b), the largest float of a's sign where b is 0 */
static inline float ee_rsqrt(float a, float b) { return gt4_vu_div(a, __gt4_sqrtf(b)); }

extern "C" f32 func_003B7DD8(void *self, Vec3 *dir, Vec3 *v, f32 power) {
    int n = gt4_ftoi(power + 0.5f, 0);   /* cvt.w.s: toward zero */
    if (n == 0) return 1.0f;
    if (n > 32) n = 32;
    float p[3], t[3];
    p[0] = v->x;
    p[1] = v->y;
    p[2] = v->z;
    gt4_rows3(t, gt4_vec(28), gt4_vec(29), gt4_vec(30), p, 3);
    const f32 x = dir->x, y = dir->y, z = dir->z;
    const f32 d = ee_rsqrt(t[0] * x + t[1] * y + t[2] * z, x * x + y * y + z * z);
    if (d < 0.0f) return -1.0f;
    f32 r = d;
    for (int i = 0; i < n - 1; i++) r *= d;
    return r;
}
