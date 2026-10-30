/* Plain vector math for the native rewrites of game functions that used VU0 code (docs/NATIVE_ONLY.md, layer 3),
 * and the state the game's functions share.
 *
 * The original kept matrices and constants in VU0's 32 vector registers across functions (the graphics library's
 * current matrix in 28..31, rotations and view rows in 20..23, bones in 8..23 while a model is skinned...;
 * tools/vu0_state_map.py lists who reads and writes which), a matrix stack in VU0's data memory and a random
 * generator in its R register. Natively these are plain globals of the runtime (runtime/shared_state.h, which
 * defines them): the 32 shared vectors, the matrix stack, the random state. Everything else a VU0 function used (the
 * registers it only uses as temporaries, ACC, Q) is a local variable of its rewrite. Sums are formed in the order the
 * VU0 code formed them (row 0 times x, plus row 1 times y, ...), so results stay within float rounding of the
 * original's.
 *
 * C++ for gcc 2.9 (game_src). */
#ifndef GT4_VEC_H
#define GT4_VEC_H

extern "C" {
extern float gt4_shared_vectors[32][4];
extern float gt4_matrix_stack[256][4];          /* rows of pushed matrices */
extern unsigned short gt4_matrix_stack_top;     /* the next free row (wraps at 256 rows) */
extern unsigned int gt4_random_state;           /* 0x3F800000 | 23 bits */
}

/* shared vector n (0..31): x, y, z, w */
inline float *gt4_vec(int n) { return gt4_shared_vectors[n]; }

/* a row of the matrix stack (the index wraps at 256) */
inline float *gt4_matrix_stack_row(unsigned int row) { return gt4_matrix_stack[row & 0xFF]; }

/* out[k] = a[k] * v[0] + b[k] * v[1] + c[k] * v[2], k < n (n 3 or 4) */
inline void gt4_rows3(float *out, const float *a, const float *b, const float *c, const float *v, int n) {
    float r[4];
    for (int k = 0; k < n; ++k) r[k] = a[k] * v[0] + b[k] * v[1] + c[k] * v[2];
    for (int k = 0; k < n; ++k) out[k] = r[k];
}

/* out[k] = a[k] * v[0] + b[k] * v[1] + c[k] * v[2] + d[k] * w, k < n: a point (w = 1) or a 4-vector through rows */
inline void gt4_rows4(float *out, const float *a, const float *b, const float *c, const float *d, const float *v,
                      float w, int n) {
    float r[4];
    for (int k = 0; k < n; ++k) r[k] = a[k] * v[0] + b[k] * v[1] + c[k] * v[2] + d[k] * w;
    for (int k = 0; k < n; ++k) out[k] = r[k];
}

/* float to fixed point with `frac` fraction bits (FTOI0/4/12/15), truncated toward zero and saturated as the VU
 * converts */
inline int gt4_ftoi(float x, int frac) {
    const float scaled = x * (float)(1 << frac);
    if (scaled >= 2147483648.0f) return 0x7FFFFFFF;
    if (scaled <= -2147483648.0f) return (int)0x80000000;
    if (scaled != scaled) return 0;
    return (int)scaled;
}

/* VU0's DIV: a / b, with the largest float of the right sign where the VU, which has no infinities, divides by 0 */
inline float gt4_vu_div(float a, float b) {
    if (b == 0.0f) {
        const int negative = ((a < 0.0f) != (b < 0.0f)) || (a == 0.0f && 1.0f / b < 0.0f);
        return negative ? -3.40282347e+38f : 3.40282347e+38f;
    }
    return a / b;
}

/* MINI/MAX (and the EE FPU's min.s/max.s) order floats as sign-magnitude integers: an all-ones exponent, such as
 * the graphics library's 0x7FFFFFFF "no limit", is a large number, not a NaN that every comparison fails. */
inline float gt4_minf(float a, float b) { return __gt4_minf(a, b); }
inline float gt4_maxf(float a, float b) { return __gt4_maxf(a, b); }

/* The 2D drawing helpers' projection (0049A200, 0049A360, 0049A4F8, 0049A6A0; VU0 microprogram 0x678 in the
 * original). The point (x, y, 0, 1) goes through the projection rows, shared vectors 16, 17 and 19; each lane is
 * clamped to vector 22 (MINI: compared as the VU orders floats, so 0x7FFFFFFF, the graphics library's default, is no
 * limit), divided by the unclamped w, scaled by vector 20 and offset by vector 21; out gets x, y, z, w in 12.4 fixed
 * point. Returns 1 / w, the Q of the vertex's texture coordinates. */
inline float gt4_project_2d(float x, float y, int out[4]) {
    const float *m0 = gt4_shared_vectors[16], *m1 = gt4_shared_vectors[17], *m3 = gt4_shared_vectors[19];
    const float *scale = gt4_shared_vectors[20], *offset = gt4_shared_vectors[21], *limit = gt4_shared_vectors[22];
    float v[4];
    for (int k = 0; k < 4; ++k) v[k] = m0[k] * x + m1[k] * y + m3[k];
    const float q = gt4_vu_div(1.0f, v[3]);
    for (int k = 0; k < 4; ++k) {
        const float clamped = __gt4_minf(v[k], limit[k]) * q;
        out[k] = gt4_ftoi(offset[k] + clamped * scale[k], 4);
    }
    return q;
}

/* A vertex colour as the same helpers write it (microprogram 0x678): each of the four floats clamped to 0..1 (VU
 * order, as MAX/MINI compare), scaled by shared vector 15 and truncated to an integer; the low bytes, R first. */
inline unsigned int gt4_colour_2d(const float *rgba) {
    const float *scale = gt4_shared_vectors[15];
    unsigned int out = 0;
    for (int k = 0; k < 4; ++k) {
        const float c = __gt4_minf(__gt4_maxf(rgba[k], 0.0f), 1.0f);
        out |= ((unsigned int)gt4_ftoi(c * scale[k], 0) & 0xFF) << (8 * k);
    }
    return out;
}

#endif
