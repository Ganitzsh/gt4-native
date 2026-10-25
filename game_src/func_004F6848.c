#include "types.h"
void *memcpy(void *, const void *, unsigned int);

s32 func_004F0C88(s32 arg0);                                /* extern */
s32 func_004F0CD0(s32, s32);                /* extern */

s32 func_004F6848(s32 arg0) {
    if (func_004F0C88(arg0) != 0) {
        return func_004F0CD0(arg0, 0x32);
    }
    return 0;
}
/* native correction of src/network/unit_00494578/func_004F6848.c: returns 0 when func_004F0C88 fails and func_004F0CD0's result (1) otherwise, as $v0 holds on the PS2 */
