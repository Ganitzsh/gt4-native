/* Plain vector math for the native rewrites of game functions that used VU0 macro instructions
 * (docs/NATIVE_ONLY.md, layer 3), and the game's shared vectors.
 *
 * The game keeps matrices and constants in VU0's 32 vector registers across functions (the graphics library's
 * current matrix in 28..31, rotations and view rows in 20..23, bones in 8..23 while a model is skinned...;
 * tools/vu0_state_map.py lists who reads and writes which). A rewrite keeps those as the 32 shared vectors below,
 * an ordinary global array, and makes everything else (the registers a function only uses as temporaries, ACC, Q)
 * local variables. Sums are formed in the order the VU0 code formed them (row 0 times x, plus row 1 times y, ...),
 * so results stay within float rounding of the original's.
 *
 * C++ for gcc 2.9 (game_src). */
#ifndef GT4_VEC_H
#define GT4_VEC_H

#include "gt4_asm.h"   /* the shared vectors' storage: __gt4_vu0.vf, until every user is rewritten */

/* shared vector n (0..31): x, y, z, w */
inline float *gt4_vec(int n) { return __gt4_vu0.vf[n]; }

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

inline float gt4_minf(float a, float b) { return a < b ? a : b; }
inline float gt4_maxf(float a, float b) { return a > b ? a : b; }

#endif
