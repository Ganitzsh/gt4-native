/* compiler: ee-gcc2.96-no-strict-aliasing */
#include "types.h"
#define NULL 0
void *memcpy(void *, const void *, unsigned int);

s32 func_004F0C88(struct func_004FEF58_arg0 *arg0);                                /* extern */
s32 func_004F0CD0(void *, s32);             /* extern */

struct func_004FEF58_arg0_unk5A8 {
    s32 unk0;
    s32 unk4;
};
struct func_004FEF58_arg0 {
    char pad0[0x5A8];
    struct func_004FEF58_arg0_unk5A8 *unk5A8;
};

s32 func_004FEF58(struct func_004FEF58_arg0 *arg0, s32 arg1, s32 arg2) {
    if (func_004F0C88(arg0) != 0) {
        arg0->unk5A8->unk0 = arg1;
        arg0->unk5A8->unk4 = arg2;
        return func_004F0CD0(arg0, 0x39);
    }
    return 0;
}
/* native correction of src/network/unit_00494578/func_004FEF58.c: returns 0 when func_004F0C88 fails and func_004F0CD0's result (1) otherwise, as $v0 holds on the PS2 */
