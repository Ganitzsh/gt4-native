/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0049C140 (unmatched draft): sets shared vectors 20..23 to the matrix (28..31 in mode 2, else the
 * one at +0xF50) and writes at the cursor (+0x1F8) a DMA cnt tag (5 quadwords) with its VIF code, the matrix and the
 * colour at +0xC24 as four floats (its bytes, 0..255); advances the cursor by 6 quadwords, size +0x18 = 0x2C1.
 * Natively the batch goes as batch records (compat/game/gt4_gfx.h) instead of DMA tags and VIF codes: the same
 * formats, lane masks and cycles, expanded by gt4_gfx_batch_unpack. */
#include "gt4_gfx.h"
#include "gt4_vec.h"
extern "C" void *memcpy(void *, const void *, unsigned int);
typedef unsigned int u32;
typedef float f32;

struct State {
    u32 f0 : 8;
    u32 current : 8;
    char pad4[0x18 - 4];
    u32 qwc;
    char pad1C[0x1F8 - 0x1C];
    u32 *cursor;
    char pad1FC[0xC24 - 0x1FC];
    u32 color;
    char padC28[0xF50 - 0xC28];
    f32 matrix[4][4];
};

extern "C" void func_0049C140(State *s) {
    u32 block[20];
    u32 *p = block - 4;
    const u32 color = s->color;
    if (s->current == 2) {
        for (int r = 0; r < 4; ++r) memcpy(gt4_vec(20 + r), gt4_vec(28 + r), 16);
    } else {
        for (int r = 0; r < 4; ++r) memcpy(gt4_vec(20 + r), s->matrix[r], 16);
    }
    for (int r = 0; r < 4; ++r) memcpy(p + 4 + 4 * r, gt4_vec(20 + r), 16);
    f32 *rgba = (f32 *)(p + 20);
    rgba[0] = (f32)(int)(color & 0xFF);
    rgba[1] = (f32)(int)((color >> 8) & 0xFF);
    rgba[2] = (f32)(int)((color >> 16) & 0xFF);
    rgba[3] = (f32)(int)(color >> 24);
    s->qwc = 0x2C1;
    gt4_gfx_batch_write(s, 0xC9, block, 5, 0xF);
}
