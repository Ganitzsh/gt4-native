/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 00450558 (draft with VU0 asm): sets the render state for the billboards, then turns the view
 * (shared vectors 20..23 as func_0049E430 leaves them) into its inverse: rows 20..22 transposed (w = 0) and 23's
 * xyz the transposed rotation applied to -23 (its w kept). D_00846590 gets the view's right axis on the ground
 * plane, (z, 0, -x) of row 22 after the transpose, normalized in x and z (its w lane, a stale VU0 temporary in
 * the original, is left as it is). Then binds the texture and the alpha state of self. */
#include "gt4_vec.h"
typedef int s32;
typedef float f32;

struct Sub {
    char pad[0x18];
    s32 f18;
};

struct Self {
    char pad[0x1C];
    Sub *f1C;
    s32 f20;
};

struct Vec4 {
    f32 x, y, z, w;
};

extern "C" int func_00499728(void *);
extern "C" void func_0049CC38(s32);
extern "C" void func_0049CE40(s32);
extern "C" void func_0049CE88(s32);
extern "C" void func_0049CEA0(void);
extern "C" void func_0049E430(void);
extern "C" void func_004A1638(s32);
extern "C" int func_004A26F0(s32);
extern "C" void func_004A2750(s32, f32);
extern "C" void func_004A27C8(s32);
extern "C" void func_004A2808(s32, float);
extern "C" void func_004AB040(s32);

extern Vec4 D_00846590;

extern "C" void func_00450558(Self *self) {
    if (self == 0)
        return;
    func_004A1638(0x11);
    func_004AB040(0xF);
    func_004A1638(2);
    func_004A1638(8);
    func_004A1638(5);
    func_004A1638(4);
    func_004AB040(3);
    func_004A1638(6);
    func_004A26F0(3);
    func_004A2750(4, 1.0f);
    func_004A27C8(1);
    func_004A2808(0x44, 0.0f);

    func_0049E430();
    float *r0 = gt4_vec(20), *r1 = gt4_vec(21), *r2 = gt4_vec(22), *t = gt4_vec(23);
    const float a0 = r0[0], a1 = r0[1], a2 = r0[2];
    const float b0 = r1[0], b1 = r1[1], b2 = r1[2];
    const float c0 = r2[0], c1 = r2[1], c2 = r2[2];
    const float t0 = 0.0f - t[0], t1 = 0.0f - t[1], t2 = 0.0f - t[2];
    /* the transpose: row k holds lane k of the old rows 20, 21, 22 */
    r0[0] = a0; r0[1] = b0; r0[2] = c0; r0[3] = 0.0f;
    r1[0] = a1; r1[1] = b1; r1[2] = c1; r1[3] = 0.0f;
    r2[0] = a2; r2[1] = b2; r2[2] = c2; r2[3] = 0.0f;
    t[0] = a0 * t0 + a1 * t1 + a2 * t2;
    t[1] = b0 * t0 + b1 * t1 + b2 * t2;
    t[2] = c0 * t0 + c1 * t1 + c2 * t2;

    /* (0, 0, 1) through the new rows is the new row 22; its cross product with (0, 1, 0) */
    Vec4 *v = &D_00846590;
    const float x = c2, z = 0.0f - a2;
    v->y = 0.0f;
    const float inv = gt4_vu_div(1.0f, __gt4_sqrtf(x * x + z * z));
    v->x = x * inv;
    v->z = z * inv;

    func_00499728(self->f1C);
    func_0049CC38((s32)func_0049CEA0);
    func_0049CE88(self->f1C->f18);
    func_0049CE40(self->f20);
}
