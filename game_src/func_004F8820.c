#include "types.h"
#define NULL 0
void *memcpy(void *, const void *, unsigned int);

struct func_004F8820_arg0 {
    char pad0[0x174];
    s32 unk174;
};

s32 func_004F8820(struct func_004F8820_arg0 *arg0, s32 arg1) {
    func_004F8790(arg0);
    arg0->unk174 = arg1;
    return arg1;
}
/* native correction of src/network/unit_00494578/func_004F8820.c: returns arg1, which $v0 still holds on the PS2 for the caller that reads it (func_004F08B8 passes func_0052E6D8's result in $v0 and $a1) */
