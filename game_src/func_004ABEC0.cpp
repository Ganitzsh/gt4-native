/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 004ABEC0 (draft with VU0 asm): sets parameter `which` of light l to v. 0, 1, 4: stored as is
 * (+0x10, +0x20, +0x40). 13 (a position, or a direction when w = 0: then normalised) and 5 (a direction, always
 * normalised, w scaled too) go through camera matrix set 0 (shared vectors 28..31 when set 0 is current, else its
 * copy at s + 0xED0) into +0x60 and +0x90; shared vectors 20..23 are left holding that matrix, as the original
 * leaves them. Then, if the light is enabled, flag bit 0 of s. */
#include "gt4_vec.h"
extern "C" void *memcpy(void *, const void *, unsigned int);
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef float f32;
typedef unsigned int u128 __attribute__((mode(TI)));

struct Scratch {
    u8 dirty;
    u8 current;
    char pad2[0x48 - 2];
    u16 flags;
    char pad4A[0xED0 - 0x4A];
    float matrix[4][4];
};

struct Light {
    char pad0[8];
    u8 enabled;
    char pad9[0x10 - 9];
    u128 v10;
    u128 v20;
    u128 v30;
    u128 v40;
    u128 v50;
    u128 v60;
    u128 v70;
    u128 v80;
    u128 v90;
};

/* v *= 1 / sqrt(x^2 + y^2 + z^2) (VU0 RSQRT; all four lanes) */
static inline void normalize(float *v) {
    float sq = v[0] * v[0];
    sq = sq + v[1] * v[1];
    sq = sq + v[2] * v[2];
    const float q = gt4_vu_div(1.0f, __gt4_sqrtf(__gt4_absf(sq)));
    for (int k = 0; k < 4; ++k)
        v[k] = v[k] * q;
}

/* out = v through camera matrix set 0, kept in shared vectors 20..23 */
static inline void through_camera(Scratch *s, const float *v, void *out) {
    for (int r = 0; r < 4; ++r)
        memcpy(gt4_vec(20 + r), s->current == 0 ? gt4_vec(28 + r) : s->matrix[r], 16);
    float o[4];
    gt4_rows4(o, gt4_vec(20), gt4_vec(21), gt4_vec(22), gt4_vec(23), v, v[3], 4);
    memcpy(out, o, 16);
}

extern "C" void func_004ABEC0(Scratch *s, Light *l, u32 which, u128 v) {
    float f[4];
    switch (which) {
    case 0:
        l->v10 = v;
        break;
    case 1:
        l->v20 = v;
        break;
    case 4:
        l->v40 = v;
        break;
    case 13:
        memcpy(f, &v, 16);
        if (f[3] == 0.0f)
            normalize(f);
        through_camera(s, f, &l->v60);
        break;
    case 5:
        memcpy(f, &v, 16);
        normalize(f);
        through_camera(s, f, &l->v90);
        break;
    }
    if (l->enabled)
        s->flags |= 1;
}
