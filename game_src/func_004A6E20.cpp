/* compiler: ee-gcc2.96-nogcse */
/* Native version of 004A6E20 (draft with VU0 asm): the fog word for a point at distance z ahead of the camera.
 * (0, 0, -z, 1) goes through camera matrix set 1 (shared vectors 28..31 when set 1 is current, else its copy in
 * the scratchpad at 0x70002F10), is clamped by the screen row 22, divided by w, its z clamped to [-1, 1], scaled
 * and offset by the screen rows 20, 21 and converted to integers: the scratchpad's fog word (0x70002248) gets z in
 * its low bits and w << 24 (0xFF << 24 when z <= 0). Only lanes z and w are used. Shared vectors 20..23 are left
 * holding the scratchpad's screen rows (0x70002FA0), as the original leaves them. */
#include "gt4_vec.h"
#include "spr.h"
extern "C" void *memcpy(void *, const void *, unsigned int);
typedef unsigned char u8;
typedef unsigned short u16;
typedef float f32;

struct Spr {
    char pad0;
    u8 current;
    char pad2[0x246];
    unsigned int fog;
    char pad24C[0xC84];
    float matrix[3][4][4];
    u16 vi13[3];
    char padF96[0xA];
    float screen[4][4];
};

extern "C" void func_004A6E20(f32 z) {
    Spr *s = (Spr *)spr_base();
    const float *m2, *m3;
    if (s->current == 1) {
        m2 = gt4_vec(30);
        m3 = gt4_vec(31);
    } else {
        m2 = s->matrix[1][2];
        m3 = s->matrix[1][3];
    }
    /* rows 0 and 1 are multiplied by 0 */
    const float nz = 0.0f - z;
    float v[4];
    v[2] = m2[2] * nz + m3[2];
    v[3] = m2[3] * nz + m3[3];
    for (int r = 0; r < 4; ++r)
        memcpy(gt4_vec(20 + r), s->screen[r], 16);
    const float *scale = gt4_vec(20), *offset = gt4_vec(21), *limit = gt4_vec(22);
    const float invw = gt4_vu_div(1.0f, v[3]);
    float rz = __gt4_minf(v[2], limit[2]) * invw;
    float rw = __gt4_minf(v[3], limit[3]) * invw;
    rz = __gt4_minf(rz, 1.0f);
    rz = __gt4_maxf(rz, -1.0f);
    const int iz = gt4_ftoi(offset[2] + rz * scale[2], 0);
    const int iw = gt4_ftoi(offset[3] + rw * scale[3], 0);
    int a = 0xFF;
    if (!(z <= 0.0f))
        a = iw;
    s->fog = ((unsigned int)a << 24) | (unsigned int)iz;
}
