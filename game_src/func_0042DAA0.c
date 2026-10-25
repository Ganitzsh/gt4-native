#include "types.h"
#define NULL 0
void *memcpy(void *, const void *, unsigned int);

s32 func_0042D9F0(void *, f32 fparg0);
f32 func_0042DC00(s32, f32);
f32 func_0042DAA0(s8 *arg0, f32 fparg0) {
    s8 *p = arg0;
    p += func_0042D9F0(arg0, fparg0) * 4;
    return func_0042DC00(*(s32 *)(p + 0xC), fparg0);
}
/* native correction of src/race/unit_004271E8/func_0042DAA0.c: returns the f32 ($f0) of the tail-called func_0042DC00, which func_0042D688 reads. */
