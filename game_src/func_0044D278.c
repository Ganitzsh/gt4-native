#include "types.h"
void *memcpy(void *, const void *, unsigned int);

s32 func_00579E00(void *, void *, s32, s32);    /* extern */

struct func_0044D278_arg0 {
    s32 unk0;
    s32 unk4;
    char pad8[0x4];
    s32 unkC;
};

s32 func_0044D278(void *arg0) {
    void *temp_v0;

    temp_v0 = arg0 + ((struct func_0044D278_arg0 *)arg0)->unk0;
    return func_00579E00(temp_v0, temp_v0, ((struct func_0044D278_arg0 *)arg0)->unk4, ((struct func_0044D278_arg0 *)arg0)->unkC);
}
/* native correction of src/race/unit_004271E8/func_0044D278.c: returns the result of the tail call to func_00579E00, left in $v0 on the PS2 */
