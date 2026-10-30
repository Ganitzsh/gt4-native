/* compiler: ee-gcc2.96-as2004 */
/* Native version of 0044DCC0 (draft with VU0 asm): after func_0049E3E0 has set up the view, the point p through
 * shared vectors 24..27 (w = 1); x and y clamped to at most shared vector 22's, divided by w and mapped from
 * [-1, 1] to vp's pixels (y down) into screen[0..1]. */
#include "gt4_vec.h"
typedef int s32;
typedef float f32;

/* three floats, 4-byte aligned */
struct Vec3w {
    f32 x, y, z;
};

struct Viewport {
    char pad[0x30];
    s32 width;
    s32 height;
};

extern "C" void func_0049E3E0(void);

extern "C" void func_0044DCC0(Viewport *vp, f32 *screen, Vec3w *p) {
    func_0049E3E0();
    const f32 x = p->x, y = p->y, z = p->z;
    const float *a = gt4_vec(24), *b = gt4_vec(25), *c = gt4_vec(26), *d = gt4_vec(27), *lim = gt4_vec(22);
    f32 vx = a[0] * x + b[0] * y + c[0] * z + d[0];
    f32 vy = a[1] * x + b[1] * y + c[1] * z + d[1];
    const f32 vw = a[3] * x + b[3] * y + c[3] * z + d[3];
    const f32 q = gt4_vu_div(1.0f, vw);
    vx = gt4_minf(vx, lim[0]) * q;
    vy = gt4_minf(vy, lim[1]) * q;
    screen[0] = (vx + 1.0f) * (f32)vp->width * 0.5f;
    screen[1] = (1.0f - vy) * (f32)vp->height * 0.5f;
}
