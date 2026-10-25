#include "types.h"
void *memcpy(void *, const void *, unsigned int);

s32 func_004F0C88(struct func_004F1740_arg0 *arg0);                                /* extern */
s32 func_004F0CD0(void *, s32);             /* extern */
s32 func_005A48D8(void *, s32, s32);    /* extern */

struct func_004F1740_arg0_unk5A8 {
    char pad0[0x18];
    s32 unk18;
};
struct func_004F1740_arg0 {
    char pad0[0x5A8];
    struct func_004F1740_arg0_unk5A8 *unk5A8;
};

s32 func_004F1740(struct func_004F1740_arg0 *arg0) {
    if (func_004F0C88(arg0) != 0) {
        func_005A48D8(arg0->unk5A8, 0, 0x1C);
        arg0->unk5A8->unk18 = 1;
        return func_004F0CD0(arg0, 6);
    }
    return 0;
}
/* native correction of src/network/unit_00494578/func_004F1740.c: returns func_004F0CD0's result (1) after the request is queued and 0 when func_004F0C88 fails, the values $v0 holds at each return */
