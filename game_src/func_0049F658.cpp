/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0049F658 (recovered matched source with VU0 asm): turns the eye direction (x, y, z) into view
 * space through the rows D_0084AFF0, D_0084B000, D_0084B010 (left in shared vector 9, as the original leaves VU0
 * vf9), derives the roll from it, roll = -((atan2(d.z, d.x) (004A5F48) + 1) / 8), stored at D_00624DA8, and rebuilds
 * the rows at D_0084AFC0: the rotation by roll * -360 degrees about y, (cos, 0, -sin), (0, 1, 0), (sin, 0, cos)
 * (sine and cosine from 004A7438, in shared vector 1), times the rows at D_0084AFF0.., then the result times the rows
 * at D_0084B020... The w lanes of the rotation rows are the stale w lanes of VU0 registers in the original (vf4..vf6
 * as earlier code left them, nearly always 0): 0 here. */
#include "gt4_vec.h"
typedef float f32;

extern f32 D_00624DA8;
extern f32 D_0084AFC0[3][4];
extern f32 D_0084AFF0[4];
extern f32 D_0084B000[4];
extern f32 D_0084B010[4];
extern f32 D_0084B020[4];
extern f32 D_0084B030[4];
extern f32 D_0084B040[4];

extern "C" f32 func_004A5F48(f32 y, f32 x);
extern "C" void func_004A7438(f32 deg);

/* out = r0 v.x + r1 v.y + r2 v.z (all four lanes) */
static void rows3(f32 *out, const f32 *r0, const f32 *r1, const f32 *r2, const f32 *v) {
    f32 r[4];
    for (int k = 0; k < 4; ++k) r[k] = r0[k] * v[0] + r1[k] * v[1] + r2[k] * v[2];
    for (int k = 0; k < 4; ++k) out[k] = r[k];
}

extern "C" void func_0049F658(f32 x, f32 y, f32 z) {
    const f32 dir[3] = {x, y, z};
    f32 *v9 = gt4_vec(9);
    rows3(v9, D_0084AFF0, D_0084B000, D_0084B010, dir);
    f32 *roll = &D_00624DA8;
    *roll = -((func_004A5F48(v9[2], v9[0]) + 1.0f) * 0.125f);
    func_004A7438(*roll * -360.0f);
    const f32 s = gt4_vec(1)[0], c = gt4_vec(1)[1];
    const f32 r4[4] = {0.0f + c, 0.0f, 0.0f - s, 0.0f};
    const f32 r5[4] = {0.0f, 1.0f, 0.0f, 0.0f};
    const f32 r6[4] = {0.0f + s, 0.0f, 0.0f + c, 0.0f};
    f32 n1[4], n2[4], n3[4];
    rows3(n1, r4, r5, r6, D_0084AFF0);
    rows3(n2, r4, r5, r6, D_0084B000);
    rows3(n3, r4, r5, r6, D_0084B010);
    rows3(D_0084AFC0[0], n1, n2, n3, D_0084B020);
    rows3(D_0084AFC0[1], n1, n2, n3, D_0084B030);
    rows3(D_0084AFC0[2], n1, n2, n3, D_0084B040);
}
