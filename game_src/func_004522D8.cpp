/* compiler: ee-gcc2.96-nsa-nosib-as2004 */
/* Native version of 004522D8 (swarm draft with VU0 asm): after func_0049E3E0 has set up the view, the point p
 * (three floats, any alignment) through shared vectors 24..27 (w = 1), x and y clamped to at most shared vector
 * 22's and divided by w; that screen position replaces entry i's previous one (at +0x94 + i * 0x38, fields 0x30
 * and 0x34), and the result is func_0057D488(-20 * the distance it moved). */
#include "gt4_vec.h"
extern "C" void *memcpy(void *, const void *, unsigned int);
struct Point {
    float x, y, z;
};
extern "C" void func_0049E3E0();
extern "C" float func_0057D488(float);

extern "C" float func_004522D8(char *o, Point *p, int i) {
    char *s = o + i * 0x38;
    func_0049E3E0();
    s += 0x94;
    float v[3];
    memcpy(v, p, 12);
    const float *a = gt4_vec(24), *b = gt4_vec(25), *c = gt4_vec(26), *d = gt4_vec(27), *lim = gt4_vec(22);
    float sx = a[0] * v[0] + b[0] * v[1] + c[0] * v[2] + d[0];
    float sy = a[1] * v[0] + b[1] * v[1] + c[1] * v[2] + d[1];
    const float w = a[3] * v[0] + b[3] * v[1] + c[3] * v[2] + d[3];
    const float q = gt4_vu_div(1.0f, w);
    sx = gt4_minf(sx, lim[0]) * q;
    sy = gt4_minf(sy, lim[1]) * q;
    float *prev = (float *)(s + 0x30);
    const float dy = sy - prev[1];
    const float dx = sx - prev[0];
    prev[0] = sx;
    prev[1] = sy;
    const float sum = dx * dx + dy * dy;
    return func_0057D488(__gt4_sqrtf(sum) * -20.0f);
}
