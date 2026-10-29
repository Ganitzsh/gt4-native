/* compiler: ee-gcc2.96-nsa-nosib-as2004 */
/* Native version of 003B7C98 (matched with VU0 asm): a point (three floats, any alignment) through the current
 * matrix (shared vectors 28..31, w = 1) into view space; behind the camera (z >= 0) or beyond `near`: 0. Else its
 * screen position and depth, and 1 if a sphere of `radius` around it touches the [-1, 1] screen square. */
#include "gt4_vec.h"
extern "C" void *memcpy(void *, const void *, unsigned int);
typedef float f32;
struct Vec3 {
    f32 x, y, z;
};
struct Camera {
    char pad[0x21B0];
    f32 m21B0;
    char pad21B4[0x21C4 - 0x21B4];
    f32 m21C4;
    char pad21C8[0x21D0 - 0x21C8];
    f32 m21D0;
    f32 m21D4;
};
extern "C" int func_003B7C98(Camera *cam, Vec3 *view, Vec3 *screen, Vec3 *in, f32 radius, f32 near) {
    float p[3], v[3];
    memcpy(p, in, 12);
    gt4_rows4(v, gt4_vec(28), gt4_vec(29), gt4_vec(30), gt4_vec(31), p, 1.0f, 3);
    memcpy(view, v, 12);
    const f32 depth = view->z;
    f32 inv = -1.0f / depth;
    if (depth >= 0.0f) return 0;
    if (depth < -near) return 0;
    const f32 sx = (view->x * cam->m21B0 + depth * cam->m21D0) * inv;
    screen->x = sx;
    const f32 sy = (view->y * cam->m21C4 + view->z * cam->m21D4) * inv;
    screen->y = sy;
    screen->z = -view->z;
    inv *= radius;
    const f32 ex = cam->m21B0 * inv, ey = cam->m21C4 * inv;
    if (sx - ex > 1.0f || sx + ex < -1.0f || sy - ey > 1.0f || sy + ey < -1.0f) return 0;
    return 1;
}
