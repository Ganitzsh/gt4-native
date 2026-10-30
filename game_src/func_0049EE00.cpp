/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0049EE00 (recovered matched source with VU0 asm): the lights in view space. Clears flag 2
 * (+0x48). The camera matrix M is the current matrix (shared vectors 28..31) when s->mode (+1) is 0, else the copy at
 * +0xED0. Its inverse goes to shared vectors 20..23, as the original leaves VU0 vf20..vf23: rows 20..22 the transposed
 * rotation (w 0), each multiplied lane by lane by the scaling at +0xBE0 when byte +0xE is set; row 23 = R^T (-t) in
 * x, y, z and M's w in w (the rows before scaling). Through it: the eye (+0xAB0) to +0xAC0 and +0x390, row 22
 * normalised to +0xAD0 (the normal), and for each light of the list at +0xB18: a direction light (pos.w 0, pos at
 * +0x60) gets its direction normalised (+0x70) and the half vector, normalise(direction + normal) (+0x80); another
 * light its position (+0x70) and its axis (+0x90) normalised (+0xA0). A light with index i >= 0 (+9) puts its direction
 * and half vector in lane i of the rows at +0x2E0 and +0x360 (cleared first). Normalising multiplies all four lanes by
 * 1 / sqrt(x^2 + y^2 + z^2) (VU0 RSQRT: the largest float for a zero length). */
#include "gt4_vec.h"
extern "C" void *memcpy(void *, const void *, unsigned int);

struct Light {
    Light *next;
    char p4[5];
    signed char index;
    char pa[0x60 - 0xa];
    float pos[4], dir[4], half[4], axis[4], axisout[4];
};

struct S {
    unsigned char dirty;
    volatile unsigned char mode;
    char p2[0xc];
    unsigned char scale;
    char pf[0x48 - 0xf];
    volatile unsigned short flags;
    char p4a[0x2e0 - 0x4a];
    float a[3][4];
    char p310[0x360 - 0x310];
    float b[3][4];
    float view[4];
    char p3a0[0xab0 - 0x3a0];
    float eye[4], eyeout[4], normal[4];
    char pae0[0xb18 - 0xae0];
    Light *lights;
    char pb1c[0xbe0 - 0xb1c];
    float scaling[4];
    char pbf0[0xed0 - 0xbf0];
    float m[4][4];
};

/* v through rows 20..23: r20 x + r21 y + r22 z + r23 w (all four lanes) */
static void through_view(float *out, const float *v) {
    const float *r0 = gt4_vec(20), *r1 = gt4_vec(21), *r2 = gt4_vec(22), *r3 = gt4_vec(23);
    float r[4];
    for (int k = 0; k < 4; ++k) r[k] = r0[k] * v[0] + r1[k] * v[1] + r2[k] * v[2] + r3[k] * v[3];
    memcpy(out, r, 16);
}

/* v times 1 / sqrt(x^2 + y^2 + z^2), all four lanes */
static void normalise(float *v) {
    const float q = gt4_vu_div(1.0f, __gt4_sqrtf(__gt4_absf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])));
    for (int k = 0; k < 4; ++k) v[k] = v[k] * q;
}

extern "C" void func_0049EE00(S *s) {
    s->flags &= ~2;
    float m[4][4];
    if (s->mode == 0) {
        for (int r = 0; r < 4; ++r) memcpy(m[r], gt4_vec(28 + r), 16);
    } else {
        memcpy(m, s->m, 64);
    }
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 4; ++k) {
            s->a[r][k] = 0.0f;
            s->b[r][k] = 0.0f;
        }
    /* the inverse: transposed rotation, translation R^T (-t) */
    const float t[3] = {0.0f - m[3][0], 0.0f - m[3][1], 0.0f - m[3][2]};
    float *r20 = gt4_vec(20), *r21 = gt4_vec(21), *r22 = gt4_vec(22), *r23 = gt4_vec(23);
    for (int c = 0; c < 3; ++c) {
        float *row = gt4_vec(20 + c);
        row[0] = m[0][c];
        row[1] = m[1][c];
        row[2] = m[2][c];
        row[3] = 0.0f;
    }
    for (int k = 0; k < 3; ++k) r23[k] = r20[k] * t[0] + r21[k] * t[1] + r22[k] * t[2];
    r23[3] = m[3][3];
    if (s->scale)
        for (int k = 0; k < 4; ++k) {
            r20[k] = r20[k] * s->scaling[k];
            r21[k] = r21[k] * s->scaling[k];
            r22[k] = r22[k] * s->scaling[k];
        }
    float v[4];
    through_view(v, s->eye);
    float normal[4];
    for (int k = 0; k < 4; ++k) normal[k] = r20[k] * 0.0f + r21[k] * 0.0f + r22[k];   /* (0, 0, 1) through the rotation */
    memcpy(s->eyeout, v, 16);
    memcpy(s->view, v, 16);
    normalise(normal);
    memcpy(s->normal, normal, 16);
    for (Light *l = s->lights; l; l = l->next) {
        if (l->pos[3] == 0.0f) {
            through_view(v, l->pos);
            normalise(v);
            memcpy(l->dir, v, 16);
            for (int k = 0; k < 4; ++k) v[k] = v[k] + normal[k];
            normalise(v);
            memcpy(l->half, v, 16);
        } else {
            through_view(v, l->pos);
            memcpy(l->dir, v, 16);
            through_view(v, l->axis);
            normalise(v);
            memcpy(l->axisout, v, 16);
        }
        const int i = l->index;
        if (i >= 0) {
            s->a[0][i] = l->dir[0];
            s->a[1][i] = l->dir[1];
            s->a[2][i] = l->dir[2];
            s->b[0][i] = l->half[0];
            s->b[1][i] = l->half[1];
            s->b[2][i] = l->half[2];
        }
    }
}
