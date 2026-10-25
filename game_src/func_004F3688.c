#include "types.h"
void *memcpy(void *, const void *, unsigned int);

s32 func_004F0C88(void *arg0);                                /* extern */
s32 func_004F0CD0(void *, s32);             /* extern */
s32 func_005A48D8(s32, s32, s32);       /* extern */
s32 func_005A6AB0(s32, void *, s32);        /* extern */

struct func_004F3688_arg0 {
    char pad0[0x5A8];
    s32 unk5A8;
};

s32 func_004F3688(void *arg0, void *arg1, void *arg2) {
    s32 temp_s0;

    if (func_004F0C88(arg0) != 0) {
        func_005A48D8(((struct func_004F3688_arg0 *)arg0)->unk5A8, 0, 0x66);
        temp_s0 = ((struct func_004F3688_arg0 *)arg0)->unk5A8;
        func_005A6AB0(temp_s0 + 0x15, arg0 + 0xAF8, 0x11);
        func_005A6AB0(temp_s0 + 0x26, arg2, 0x20);
        func_005A6AB0(temp_s0 + 0x46, arg1, 0x20);
        return func_004F0CD0(arg0, 0x23);
    }
    return 0;
}
/* native correction of src/network/unit_00494578/func_004F3688.c: returns func_004F0CD0's result (1) after the request is queued and 0 when func_004F0C88 fails, the values $v0 holds at each return */
