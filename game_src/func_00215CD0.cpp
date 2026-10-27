/* compiler: ee-gcc2.96 */
/* Native copy of mRenderContextPS2 virtual 51 (00215CD0, matched): the menus' filled rectangle (x, y, w,
 * h in the 640 x 480 menu layout; the colour and screen transitions fill 0, 0, 640, 480). With the menus
 * in a centred 4:3 area (game_src/func_00214FB0.cpp, the wide mode) a rectangle that spans the layout's
 * whole width reaches the wide frame's edges instead, so fades and full-width bars cover the screen. */
#include "types.h"

s32 func_002314E8(void *);                      /* extern */
s32 func_00499D00(s32, f32, f32, f32, f32);     /* extern */
extern s32 D_006184F0;
extern "C" int gt4_display_aspect_milli(void);
extern "C" int gt4_menus_centered(void);

void mRenderContextPS2__virtual_51(void *self, f32 x, f32 y, f32 w, f32 h) {
    const int aspect = gt4_display_aspect_milli();
    if (D_006184F0 && aspect >= 1500 && gt4_menus_centered() && x <= 0.0f && x + w >= 640.0f) {
        const f32 half = 240.0f * (f32)aspect / 1000.0f;
        x = 320.0f - half;
        w = 2.0f * half;
    }
    func_00499D00(func_002314E8(self), x, y, w, h);
}
