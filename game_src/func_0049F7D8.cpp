/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0049F7D8 (VU0 asm): a camera basis from a direction (x, y, z) and an up vector (ux, uy, uz):
 * d = the direction normalised, s = up x d normalised, u = d x s (VU0's RSQRT: 1/sqrt of the squared length, the
 * largest float for 0); D_0084AFF0's rows = u, d, s. Then from the camera matrix M (the current matrix, shared
 * vectors 28..31, when the scratchpad's current byte (spr +1) is 0, else the scratchpad matrix at spr +0xED0):
 * D_0084B020's rows = M's transposed rotation (columns, w 0), D_0084B050 = (Mt * -t, M[3].w), and 0049F658 gets
 * minus M's third column (m02, m12, m22). Leaves shared vectors 20..23 = those four rows, as the original does.
 * The w lanes of D_0084AFF0's rows were never set by the original (stack words and the stale w of VU0's vf4, times
 * the normalising factor); here they are 0. */
#include "gt4_vec.h"
#include "spr.h"
extern "C" void *memcpy(void *, const void *, unsigned int);
typedef unsigned char u8;

struct Scratch {
    u8 dirty;
    u8 current;
    char pad2[0xED0 - 2];
    float matrix[4][4];
};

extern float D_0084AFF0[3][4];
extern float D_0084B020[3][4];
extern float D_0084B050[4];

extern "C" void func_0049F658(float x, float y, float z);

/* VU0's OPMULA/OPMSUB pair: a x b */
static void cross(float *out, const float *a, const float *b) {
    float r[3];
    r[0] = a[1] * b[2] - b[1] * a[2];
    r[1] = a[2] * b[0] - b[2] * a[0];
    r[2] = a[0] * b[1] - b[0] * a[1];
    out[0] = r[0];
    out[1] = r[1];
    out[2] = r[2];
}

/* v times VU0's RSQRT of its squared length */
static void normalise(float *v) {
    float len2 = v[0] * v[0];
    len2 += v[1] * v[1];
    len2 += v[2] * v[2];
    const float q = gt4_vu_div(1.0f, __gt4_f_sqrt(len2 < 0.0f ? -len2 : len2));
    for (int k = 0; k < 3; ++k) v[k] *= q;
}

extern "C" void func_0049F7D8(float x, float y, float z, float ux, float uy, float uz) {
    const Scratch *spr = (const Scratch *)spr_base();
    float up[3], dir[3], side[3], u[3];
    up[0] = ux;
    up[1] = uy;
    up[2] = uz;
    dir[0] = x;
    dir[1] = y;
    dir[2] = z;
    cross(side, up, dir);
    normalise(dir);
    normalise(side);
    cross(u, dir, side);
    for (int k = 0; k < 3; ++k) {
        D_0084AFF0[2][k] = side[k];
        D_0084AFF0[1][k] = dir[k];
        D_0084AFF0[0][k] = u[k];
    }
    D_0084AFF0[0][3] = D_0084AFF0[1][3] = D_0084AFF0[2][3] = 0.0f;

    float m[4][4];
    if (spr->current == 0) {
        for (int r = 0; r < 4; ++r) memcpy(m[r], gt4_vec(28 + r), 16);
    } else {
        memcpy(m, spr->matrix, 64);
    }
    float t[4][4];
    for (int c = 0; c < 3; ++c) {
        for (int k = 0; k < 3; ++k) t[c][k] = m[k][c];
        t[c][3] = 0.0f;
    }
    const float nx = 0.0f - m[3][0], ny = 0.0f - m[3][1], nz = 0.0f - m[3][2];
    for (int k = 0; k < 3; ++k) t[3][k] = t[0][k] * nx + t[1][k] * ny + t[2][k] * nz;
    t[3][3] = m[3][3];
    memcpy(D_0084B020, t, 48);
    float v2[4];
    for (int k = 0; k < 4; ++k) v2[k] = 0.0f + t[3][k];
    memcpy(D_0084B050, v2, 16);
    /* vf1 = (0, 0, -1) through the transposed rotation: -(t[2]), x, y, z */
    float f[3];
    for (int k = 0; k < 3; ++k) f[k] = 0.0f + t[2][k] * -1.0f;
    for (int r = 0; r < 4; ++r) memcpy(gt4_vec(20 + r), t[r], 16);
    func_0049F658(f[0], f[1], f[2]);
}
