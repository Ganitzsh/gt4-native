/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 00456918 (draft with VU0 asm): the screen height of one unit at unit depth, for LOD
 * (D_008465E4 = |y1 - y0| * 10 / 7 * scale): the points (0, 0, -1) and (0, -1, -1) through matrix 1 (the current
 * matrix, shared vectors 28..31, while func_004A5348(1) has it selected), then, after func_004A5348(0), their y
 * scaled by shared vector 20's and offset by 21's (as func_0049E3E0 loaded them).
 * Not the original's: the host's effects.model_detail (gt4_game_option(2): 0 original, 1 high, 2 ultra) scales
 * it by 1, 2 or 4. The model interpreter (00455708) picks a model's level from this height over the distance
 * (00454FB0: each level at half the size of the one before), so its detailed levels are kept that much farther. */
#include "gt4_vec.h"
extern "C" void func_0049E3E0();
extern "C" void func_004A5348(int);
extern "C" float D_008465E4;
extern "C" int gt4_game_option(int id); /* runtime/sdk.cpp: the host's options */

extern "C" void func_00456918(float scale) {
    func_0049E3E0();
    func_004A5348(1);
    /* (0, 0, -1, 1) and (0, -1, -1, 1) through rows 28..31, lane y */
    const float *m28 = gt4_vec(28), *m29 = gt4_vec(29), *m30 = gt4_vec(30), *m31 = gt4_vec(31);
    const float y0 = m28[1] * 0.0f + m29[1] * 0.0f + m30[1] * -1.0f + m31[1];
    const float y1 = m28[1] * 0.0f + m29[1] * -1.0f + m30[1] * -1.0f + m31[1];
    func_004A5348(0);
    const float *s = gt4_vec(20), *o = gt4_vec(21);
    const float fx = o[1] + y0 * s[1];
    const float fy = o[1] + y1 * s[1];
    const int detail = gt4_game_option(2);
    D_008465E4 = __gt4_absf((fy - fx) * 1.42857134f) * scale * (detail == 2 ? 4.0f : detail == 1 ? 2.0f : 1.0f);   /* 0x3FB6DB6D */
}
