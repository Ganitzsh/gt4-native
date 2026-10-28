/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native code for 003C7CD8 (matched; src/race/RacePhotoMode/func_003C7CD8.cpp, corrected for the native
 * calling convention): draws the photo mode's vignette disc (func_003BBBC0, radii 420 and 200) in the colour
 * built by func_004AA0D0 from the lens factor at +0x1E66C (* 0.7 * 0.01).
 * The source declares func_004AA0D0 (void *, void *, f32 x5); the callee takes only the five floats
 * (r, g, b, a, scale in $f12-$f16, game_src/func_004AA0D0.cpp) and never reads $a0/$a1. The original's jal
 * at 0x3C7D24 passes $f12 = $f13 = $f14 = 0, $f15 = the factor, $f16 = 255. Natively the callee read the two
 * pointers as r and g and every float one slot early (a = 0, scale = 0: a black transparent colour). */
#include "types.h"

extern "C" void func_003BBBC0(void *renderContext, u32 innerColor, u32 outerColor, f32 innerRadius, f32 outerRadius);
extern "C" s32 func_004AA0D0(f32 posX, f32 posY, f32 posZ, f32 radiusFactor, f32 maxAlpha);

extern "C" void func_003C7CD8(void *photoContext, void *renderContext) {
    f32 radiusFactor;

    radiusFactor = *(f32 *)((char *)photoContext + 0x1E66C) * 0x1.666666p-1f * 0x1.47AE14p-7f;
    func_003BBBC0(renderContext, (u32)func_004AA0D0(0.0f, 0.0f, 0.0f, radiusFactor, 255.0f), 0, 420.0f, 200.0f);
}
