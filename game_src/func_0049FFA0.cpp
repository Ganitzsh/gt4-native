/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0049FFA0 (VU0 asm): resets the graphics library's state block. 004A7454 sets the current
 * matrix (shared vectors 28..31) to the identity, which is copied into the three matrix slots at +0xED0, +0xF10 and
 * +0xF50; the matrix stack pointer (VU0 vi13) goes back to 0; then the defaults: +0xFC0..+0xFCC = D_006AF790,
 * +0x2DC, +0xFD8, +0xFDC = 1, +0xFD0, +0xFD4 = 0.5, +0xFBC = 255, halfwords +0xF90/F92/F94 = 0, 0x40, 0x50, bytes
 * +0 = 1, +1 = +3 = 0, +0x2D8 = 0, and D_00628DC0 = D_00628DC8. */
#include "gt4_vec.h"
extern "C" void *memcpy(void *, const void *, unsigned int);
typedef int s32;
typedef short s16;
typedef float f32;

struct Self {
    char b0;
    char b1;
    char b2;
    char b3;
    char pad4[0x2D8 - 4];
    s32 f2D8;
    f32 f2DC;
    char pad2E0[0xED0 - 0x2E0];
    f32 m[3][4][4];
    s16 hF90;
    s16 hF92;
    s16 hF94;
    char padF96[0xFBC - 0xF96];
    f32 fFBC;
    f32 fFC0;
    f32 fFC4;
    f32 fFC8;
    f32 fFCC;
    f32 fFD0;
    f32 fFD4;
    f32 fFD8;
    f32 fFDC;
};

extern "C" void func_004A7454(void);
extern f32 D_006AF790;
extern char D_00628DC8[];
extern char *D_00628DC0;

extern "C" void func_0049FFA0(Self *self) {
    func_004A7454();
    for (int k = 0; k < 3; ++k)
        for (int r = 0; r < 4; ++r) memcpy(self->m[k][r], gt4_vec(28 + r), 16);
    __gt4_vu0.vi[13] = 0;
    const f32 v = D_006AF790;
    self->f2DC = 1.0f;
    self->hF92 = 0x40;
    self->hF94 = 0x50;
    self->fFCC = v;
    self->fFBC = 255.0f;
    self->fFD4 = 0.5f;
    self->b0 = 1;
    self->hF90 = 0;
    self->fFC0 = v;
    self->fFC4 = v;
    self->fFC8 = v;
    self->fFD0 = 0.5f;
    self->fFD8 = 1.0f;
    self->fFDC = 1.0f;
    self->b1 = 0;
    self->b3 = 0;
    self->f2D8 = 0;
    D_00628DC0 = D_00628DC8;
}
