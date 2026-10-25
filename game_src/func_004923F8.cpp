#include "types.h"
struct G { char p0[0x54]; f32 f54; f32 f58; char p5c[0x80 - 0x5c]; s32 f80; };
struct W { char p[0xC]; u16 w; u16 h; };
extern G *PDISTD__global_font_manager;
extern f32 D_0062498C;
extern f32 D_00624990;
extern "C" void func_00499948(f32, f32, f32, f32, f32, f32, f32, f32);
extern "C" void func_004A6668(f32, f32, f32, f32, f32, f32, f32, f32);
extern "C" void func_004923F8(W *a, f32 x0, f32 y0, f32 x1, f32 y1) {
    G *g = PDISTD__global_font_manager;
    if (g->f80 != 0) {
        func_00499948(x0, y0, x1, y1, 0.0f, 0.0f, (f32)a->w, (f32)a->h);
        return;
    }
    func_004A6668((x0 + g->f54) * D_0062498C, (y0 + g->f58) * D_00624990,
                  (x1 + g->f54) * D_0062498C, (y1 + g->f58) * D_00624990, 0.0f, 0.0f, (f32)a->w, (f32)a->h);
}
/* native correction of src/engine/unit_0046A050/func_004923F8.cpp: calls func_004A6668 with its eight floats only, dropping the two ints it never reads ($a0/$a1 on the PS2) */
