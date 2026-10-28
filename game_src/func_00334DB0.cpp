/* compiler: ee-gcc2.96 */
/* Native code for 00334DB0 (matched; src/race/unit_00330800/func_00334DB0.cpp, corrected for the native
 * calling convention): when the strength at +0x12C is positive, makes a black colour with alpha
 * t * 0.7 * 0.01 (004AA0D0) and draws a ring with it (003BBBC0, radii 420 and 200).
 * 004AA0D0 takes only floats (r, g, b, a, scale): the original's jal at 0x334E00 passes them in
 * $f12..$f16 (0, 0, 0, t * 0.007, 255; 0x334DC4-0x334E04) and leaves $a0/$a1 as they are, which the
 * matched source wrote as two leading pointer arguments. Natively those pointers took the first two
 * float slots: r and g read the bits of arg0 and arg1, and b, a and scale the three zeros (the alpha
 * and 255 were never read). */
#include "types.h"

struct RS {
    char pad[0x12C];
    f32 f12C;
};

extern "C" s32 func_004AA0D0(f32 f12, f32 f13, f32 f14, f32 f15, f32 f16);
extern "C" void func_003BBBC0(void *self, s32 inner, s32 outer, f32 innerRadius, f32 outerRadius);

extern "C" void func_00334DB0(void *arg0, void *arg1) {
    f32 t = ((RS *)arg0)->f12C;
    if (0.0f < t) {
        s32 r = func_004AA0D0(0.0f, 0.0f, 0.0f, t * 0.7f * 0.01f, 255.0f);
        func_003BBBC0(arg1, r, 0, 420.0f, 200.0f);
    }
}
