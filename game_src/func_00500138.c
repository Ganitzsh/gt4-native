#include "types.h"
void *memcpy(void *, const void *, unsigned int);

s32 func_004F0C88(struct func_00500138_arg0 *arg0);                                /* extern */
s32 func_004F0CD0(void *, s32);             /* extern */
s32 func_005A48D8(void *, s32, s32);    /* extern */

struct func_00500138_arg0_unk5A8 {
    char pad0[0x18];
    s32 unk18;
};
struct func_00500138_arg0 {
    char pad0[0x5A8];
    struct func_00500138_arg0_unk5A8 *unk5A8;
};

s32 func_00500138(struct func_00500138_arg0 *arg0) {
    if (func_004F0C88(arg0) != 0) {
        func_005A48D8(arg0->unk5A8, 0, 0x4C);
        arg0->unk5A8->unk18 = 1;
        return func_004F0CD0(arg0, 0x11);
    }
    return 0;
}
/* native correction of src/network/unit_00494578/func_00500138.c: returns func_004F0CD0's result, or 0 (func_004F0C88's result) when it is not called */
