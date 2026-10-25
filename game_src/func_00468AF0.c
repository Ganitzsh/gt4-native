#include "types.h"
#define NULL 0
void *memcpy(void *, const void *, unsigned int);

s32 func_00468860(s32, s32, s32, s32, f32);
s32 func_00468A40(s32 arg0);
s32 func_00468A60(s32);
s32 func_00468AF0(s32 arg0, s32 arg1, f32 fparg0) {
    s32 temp_s1;
    temp_s1 = func_00468A40(arg0);
    return func_00468860(arg0 + 0x8C, arg1, temp_s1, func_00468A60(arg0), fparg0);
}
/* native correction of src/engine/unit_00463840/func_00468AF0.c (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_00468860 */
