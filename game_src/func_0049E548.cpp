/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0049E548 (recovered matched source with VU0 asm): the batch's setup constants, eight qwords
 * from qword `reg` (193): the view matrix (shared vectors 24..27; rows 24..26 times scale when scale is not 0) and
 * the four rows at s + 0xFA0; stores scale at +0x20. The original wrote them in the packet behind a DMA cnt tag
 * and an UNPACK V4-32 code; natively they are a batch write (compat/game/gt4_gfx.h). */
#include "gt4_gfx.h"
#include "gt4_vec.h"
extern "C" void *memcpy(void *, const void *, unsigned int);
typedef float f32;
typedef unsigned int u32;

struct Packet {
    u32 tag[4];
    f32 mat[4][4];
    f32 m2[4][4];
};

struct State {
    char pad0[0x20];
    f32 scale;
    char pad24[0x1F8 - 0x24];
    Packet *cur;
    char pad1FC[0xFA0 - 0x1FC];
    f32 m[4][4];
};

extern "C" void func_0049E548(State *s, u32 reg, f32 scale) {
    Packet packet;
    Packet *p = &packet;
    s->scale = scale;
    if (scale != 0.0f) {
        for (int r = 0; r < 3; ++r)
            for (int k = 0; k < 4; ++k) p->mat[r][k] = gt4_vec(24 + r)[k] * scale;
        memcpy(p->mat[3], gt4_vec(27), 16);
    } else {
        for (int r = 0; r < 4; ++r) memcpy(p->mat[r], gt4_vec(24 + r), 16);
    }
    memcpy(p->m2, s->m, 64);
    gt4_gfx_batch_write(s, reg & 0x3FF, p->mat, 8, 0xF);
}
