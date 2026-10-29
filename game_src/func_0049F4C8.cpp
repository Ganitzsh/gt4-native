/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0049F4C8 (VU0 asm): from the camera matrix M (the current matrix, shared vectors 28..31, when
 * s->current (+1) is 0, else s->matrix at +0xED0) builds the matrix at D_0084AF80: rows 0..2 = R0 * M[r].x +
 * R1 * M[r].y + R2 * M[r].z with R the rows at D_0084AFC0..D_0084AFE0; row 3 = M's translation moved back through
 * the transposed rotation, (Mt * -t, M[3].w), times 1/scale when scale is not 0; then the w lanes of the four rows
 * become -0.125, D_00624DA8 + 0.875, D_00624DAC * -1/6 and D_00624DB0 + 0.5. Leaves shared vectors 20..22 = M's
 * transposed rotation (columns, w 0) and 23 = (Mt * -t, M[3].w), as the original does. */
#include "gt4_vec.h"
extern "C" void *memcpy(void *, const void *, unsigned int);
typedef unsigned char u8;
typedef unsigned int u32;

struct Scratch {
    u8 dirty;
    u8 current;
    char pad2[0xED0 - 2];
    float matrix[4][4];
};

extern float D_0084AF80[4][4];
extern float D_0084AFC0[4];
extern float D_0084AFD0[4];
extern float D_0084AFE0[4];
extern float D_00624DA8;
extern float D_00624DAC;
extern float D_00624DB0;

extern "C" void func_0049F4C8(Scratch *s, float scale) {
    float m[4][4];
    if (s->current == 0) {
        for (int r = 0; r < 4; ++r) memcpy(m[r], gt4_vec(28 + r), 16);
    } else {
        memcpy(m, s->matrix, 64);
    }
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 3; ++k)
            D_0084AF80[r][k] = D_0084AFC0[k] * m[r][0] + D_0084AFD0[k] * m[r][1] + D_0084AFE0[k] * m[r][2];

    /* the transposed rotation: column c of M's rows 0..2, w 0 */
    float t[3][4];
    for (int c = 0; c < 3; ++c) {
        for (int k = 0; k < 3; ++k) t[c][k] = m[k][c];
        t[c][3] = 0.0f;
    }
    const float nx = 0.0f - m[3][0], ny = 0.0f - m[3][1], nz = 0.0f - m[3][2];
    float v23[4];
    for (int k = 0; k < 3; ++k) v23[k] = t[0][k] * nx + t[1][k] * ny + t[2][k] * nz;
    v23[3] = m[3][3];
    float row3[4];
    for (int k = 0; k < 4; ++k) row3[k] = 0.0f + v23[k];
    if (scale != 0.0f) {
        const float inv = 1.0f / scale;
        for (int k = 0; k < 4; ++k) row3[k] *= inv;
    }
    for (int k = 0; k < 3; ++k) D_0084AF80[3][k] = row3[k];

    union {
        u32 u;
        float f;
    } sixth;
    sixth.u = 0xBE2AAAAAu;   /* -1/6 */
    D_0084AF80[0][3] = -0.125f;
    D_0084AF80[1][3] = D_00624DA8 + 0.875f;
    D_0084AF80[2][3] = D_00624DAC * sixth.f;
    D_0084AF80[3][3] = D_00624DB0 + 0.5f;

    for (int c = 0; c < 3; ++c) memcpy(gt4_vec(20 + c), t[c], 16);
    memcpy(gt4_vec(23), v23, 16);
}
