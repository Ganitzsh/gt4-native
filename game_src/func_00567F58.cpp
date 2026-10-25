#include "types.h"

extern "C" s32 func_005A5600(void);

extern "C" s32 func_00567F58(s32 *arg0, s32 arg1, f32 fparg0) {
    s32 *var_s0;
    s32 var_s1;
    s32 last = 0; /* PS2: $v0 untouched (the caller's) when arg1 <= 0 */

    if (arg1 > 0) {
        var_s0 = arg0;
        var_s1 = arg1;
        do {
            var_s1 -= 1;
            s32 r = func_005A5600();
            last = r;
            f32 v = (f32)*var_s0;
            f32 t = fparg0 * 0x1.8p15f;
            f32 w = (f32)r / 0x1.FFFFFEp30f - 0.5f;
            v = v + t * w;
            *var_s0 = (s32)v;
            var_s0 += 1;
        } while (var_s1 != 0);
    }
    return last;
}
/* native correction of src/libstream/unit_005676B0/func_00567F58.cpp: returns the last func_005A5600 result, or 0 when the loop does not run (stale caller value on the PS2) */
