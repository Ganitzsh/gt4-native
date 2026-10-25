/* compiler: ee-gcc2.96-nosib */
#include "types.h"
#define NULL 0
void *memcpy(void *, const void *, unsigned int);
#include "m2c_macros.h"

s32 func_005038F0(void *, s32);                        /* extern */

s32 func_00503ED0(s32 arg0, s32 arg1) {
    s32 r;
    if ((r = func_005038F0(arg0 + 0x3C, arg1)) == 0) {
        r = func_005038F0(arg0 + 0x48, arg1);
    }
    return r;
}
/* native correction of src/network/unit_00494578/func_00503ED0.c: returns the result of the last func_005038F0 evaluated (the found entry) */
