/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0049F1F8 (VU0 asm): builds s->view (+0xB20, four rows) from the camera matrix M (the current
 * matrix, shared vectors 28..31, when s->current (+1) is 0, else s->matrix at +0xED0): rows 0..2 are M's (turned by
 * the rotation at +0xB60 when byte +0xF is set: row = R0 * row.x + R1 * row.y + R2 * row.z); row 3 is the
 * translation moved back through the transposed rotation, -(M0 * t.x + M1 * t.y + M2 * t.z) lane by lane for
 * x, y, z, then multiplied by +0xBE0 when byte +0xE is set, and by 1/scale when scale is not 0. Leaves shared
 * vectors 20..23 = the four rows, as the original does.
 * Row 3's w: the original adds a w it never set, that of VU0 register vf4 as the caller left it (vf4 is otherwise a
 * temporary here); this reads that lane of the register file, so it stays what it was. */
#include "gt4_vec.h"
extern "C" void *memcpy(void *, const void *, unsigned int);
typedef unsigned char u8;

struct Scratch {
    u8 dirty;
    u8 current;
    char pad2[0xE - 2];
    u8 scaled;
    u8 rotated;
    char pad10[0xB20 - 0x10];
    float view[4][4];
    float rot[3][4];
    char padB90[0xBE0 - 0xB90];
    float scale[4];
    char padBF0[0xED0 - 0xBF0];
    float matrix[4][4];
};

extern "C" void func_0049F1F8(Scratch *s, float scale) {
    float m[4][4];
    if (s->current == 0) {
        for (int r = 0; r < 4; ++r) memcpy(m[r], gt4_vec(28 + r), 16);
    } else {
        memcpy(m, s->matrix, 64);
    }
    /* row 3: the transposed rotation (columns of M's rows 0..2) times -t */
    const float tx = 0.0f - m[3][0], ty = 0.0f - m[3][1], tz = 0.0f - m[3][2];
    float t[4];
    for (int k = 0; k < 3; ++k) t[k] = m[k][0] * tx + m[k][1] * ty + m[k][2] * tz;
    t[3] = gt4_vec(4)[3];
    for (int k = 0; k < 4; ++k) t[k] = 0.0f + t[k];
    if (s->scaled)
        for (int k = 0; k < 4; ++k) t[k] *= s->scale[k];
    if (scale != 0.0f) {
        const float inv = 1.0f / scale;
        for (int k = 0; k < 4; ++k) t[k] *= inv;
    }
    if (s->rotated) {
        float r3[3][4];
        for (int r = 0; r < 3; ++r)
            for (int k = 0; k < 4; ++k)
                r3[r][k] = s->rot[0][k] * m[r][0] + s->rot[1][k] * m[r][1] + s->rot[2][k] * m[r][2];
        memcpy(m, r3, 48);
    }
    memcpy(m[3], t, 16);
    for (int r = 0; r < 4; ++r) memcpy(gt4_vec(20 + r), m[r], 16);
    memcpy(s->view, m, 64);
}
