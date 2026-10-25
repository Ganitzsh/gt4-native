#include "types.h"

extern "C" s32 func_00494578(char *arg0, s32 arg1);

extern "C" s32 func_004945C0(char *arg0) {
    s32 r = func_00494578(arg0, 0);
    *(s32 *)(arg0 + 0x984) = 0;
    return r;
}
/* native correction of src/network/unit_00494578/func_004945C0.cpp: returns func_00494578's result (-1), left in $v0 on the PS2 */
