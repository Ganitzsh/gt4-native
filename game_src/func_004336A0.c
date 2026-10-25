#include "types.h"
void *memcpy(void *, const void *, unsigned int);

s32 func_004336A0(s32 arg0, s32 arg1) {
    s32 var_a2;
    u8 *temp_v0;
    u8 *var_a3;

    var_a3 = arg0 + 0x228;
    var_a2 = 0;
    do {
        temp_v0 = arg1 + var_a2;
        var_a2 += 1;
        *var_a3 = *temp_v0;
        var_a3 += 1;
    } while (var_a2 < 8);
    return (s32)temp_v0;
}
/* native correction of src/race/unit_004271E8/func_004336A0.c: returns arg1 + 7, the last source address left in $v0 on the PS2 (read through func_00468860) */
