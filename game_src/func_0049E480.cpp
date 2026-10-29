/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0049E480 (VU0 asm, VU0 microprogram 0x858): refreshes the view matrix, shared vectors 24..27,
 * as A * B: row r = A0 * B[r].x + A1 * B[r].y + A2 * B[r].z + A3 * B[r].w, then rows 0..2 scaled by s->scale's x,
 * y, z (+0xBD0; 1 unless byte +0xE is set). By s->current (+1): 0, A = s->matrix[1] (+0xF10), B = the current
 * matrix (shared vectors 28..31); 1, A = the current matrix, B = s->matrix[0] (+0xED0); else A = s->matrix[1],
 * B = s->matrix[0]. Then clears the dirty bytes (+0, +3) and sets bit 1 of the flags (+0x48). */
#include "gt4_vec.h"
typedef unsigned char u8;
typedef unsigned short u16;

struct Scratch {
    u8 dirty;
    u8 current;
    char pad2;
    u8 f3;
    char pad4[0xA];
    u8 fE;
    char padF[0x39];
    u16 flags;
    char pad4A[0xB86];
    float scale[4];
    char padBE0[0x2F0];
    float matrix[2][4][4];
};

extern "C" void func_0049E480(Scratch *s) {
    const float *a[4], *b[4];
    for (int r = 0; r < 4; ++r) {
        if (s->current == 0) {
            a[r] = s->matrix[1][r];
            b[r] = gt4_vec(28 + r);
        } else if (s->current == 1) {
            a[r] = gt4_vec(28 + r);
            b[r] = s->matrix[0][r];
        } else {
            a[r] = s->matrix[1][r];
            b[r] = s->matrix[0][r];
        }
    }
    float m[4][4];
    for (int r = 0; r < 4; ++r)
        for (int k = 0; k < 4; ++k)
            m[r][k] = a[0][k] * b[r][0] + a[1][k] * b[r][1] + a[2][k] * b[r][2] + a[3][k] * b[r][3];
    if (s->fE) {
        for (int r = 0; r < 3; ++r)
            for (int k = 0; k < 4; ++k) m[r][k] *= s->scale[r];
    }
    for (int r = 0; r < 4; ++r) {
        float *v = gt4_vec(24 + r);
        for (int k = 0; k < 4; ++k) v[k] = m[r][k];
    }
    s->dirty = 0;
    s->f3 = 0;
    s->flags |= 2;
}
