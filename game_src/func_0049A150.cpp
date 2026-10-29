/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0049A150 (matched with VU0 asm): draws n vertices through one of the graphics library's 0x678
 * functions. With per-vertex colours (e != 0) it first sets the colour scale, ctx +0xFE0 and shared vector 15 (which
 * microprogram 0x678 multiplies the colours by), to (255, 255, 255, alpha * 128), then draws textured (d, the
 * texture coordinates, != 0: 0049A6A0) or not (0049A4F8); without colours, 0049A360 (textured) or 0049A200. */
#include "gt4_vec.h"
typedef float f32;
typedef int s32;

struct Ctx {
    char pad0[0xFE0];
    f32 color[4];
};

extern "C" void func_0049A200(Ctx *, s32, s32, s32, s32, s32);
extern "C" void func_0049A360(Ctx *, s32, s32, s32, s32, s32);
extern "C" void func_0049A4F8(Ctx *, s32, s32, s32, s32);
extern "C" void func_0049A6A0(Ctx *, s32, s32, s32, s32, s32);

extern "C" void func_0049A150(Ctx *ctx, s32 a, s32 b, s32 c, s32 d, s32 e, f32 alpha) {
    if (e) {
        ctx->color[0] = 255.0f;
        ctx->color[1] = 255.0f;
        ctx->color[2] = 255.0f;
        ctx->color[3] = alpha * 128.0f;
        float *scale = gt4_vec(15);
        for (int k = 0; k < 4; ++k) scale[k] = ctx->color[k];
        if (d) {
            func_0049A6A0(ctx, a, b, c, d, e);
            return;
        }
        func_0049A4F8(ctx, a, b, c, e);
        return;
    }
    if (d) {
        func_0049A360(ctx, a, b, c, d, e);
        return;
    }
    func_0049A200(ctx, a, b, c, d, e);
}
