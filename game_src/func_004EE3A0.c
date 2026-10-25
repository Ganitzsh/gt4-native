#include "types.h"
#define NULL 0
void *memcpy(void *, const void *, unsigned int);
#include "m2c_macros.h"

s32 func_004ED888(void *, M2C_UNK, s32, s32, M2C_UNK);      /* extern */

s32 func_004EE3A0(void *arg0, s32 arg1) {
    return M2C_FIELD(arg0, s32 *, 0x5C) = func_004ED888(arg0, 2, arg1, arg0 + 0x4C, 0x1D0);
}
/* native correction of src/network/unit_00494578/func_004EE3A0.c: returns the func_004ED888 result it stores, which $v0 holds at jr $ra */
