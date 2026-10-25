#include "types.h"
#define NULL 0
void *memcpy(void *, const void *, unsigned int);

s32 func_002319C0(char *);                          /* extern */
s32 mOSKeyboard__open(s32, void *, s32, f32, f32, f32, f32); /* extern */

struct func_00232120_arg0 {
    char pad0[0x1D58];
    s32 unk1D58;
};

s32 func_00232120(struct func_00232120_arg0 *arg0, f32 fparg0, f32 fparg1, f32 fparg2, f32 fparg3) {
    s32 temp_s0;

    if (func_002319C0(arg0) != 0) {
        temp_s0 = arg0->unk1D58;
        return mOSKeyboard__open(temp_s0, arg0, func_002319C0(arg0), fparg0, fparg1, fparg2, fparg3);
    }
    return 0;
}
/* native correction of src/ui/unit_0022BC20/func_00232120.c: returns s32: mOSKeyboard__open's result, or 0 (func_002319C0's zero in $v0) when there is nothing to open */
