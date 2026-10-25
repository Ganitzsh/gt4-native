#include "types.h"

extern s32 D_00622A18;
extern char D_00845C10[];

extern "C" s32 func_00400F38(void *a0);
extern "C" f32 func_00400F70(void *a0, f32 f);

extern "C" f32 func_00400FA0(f32 fparg0) {
    s32 *flag = &D_00622A18;
    if (*flag == 0) {
        func_00400F38(D_00845C10);
        *flag = 1;
    }
    return func_00400F70(D_00845C10, fparg0);
}
/* native correction of src/race/unit_003F7150/func_00400FA0.cpp: returns the f32 ($f0) of the tail-called func_00400F70 (defined f32), which func_00401008 reads. */
