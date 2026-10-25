#include "types.h"
#define NULL 0
void *memcpy(void *, const void *, unsigned int);
#include "m2c_macros.h"

s32 func_004ED888(void *, M2C_UNK, s32, s32, M2C_UNK); /* extern */

s32 func_004EE330(void *arg0, s32 arg1) {
    return M2C_FIELD(arg0, s32 *, 0x54) = func_004ED888(arg0, 0, arg1, arg0 + 0x44, 0x70);
}
/* native correction of src/network/unit_00494578/func_004EE330.c: returns the func_004ED888 result it stores, which $v0 holds at jr $ra */
