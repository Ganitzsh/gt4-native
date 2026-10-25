#include "types.h"
void *memcpy(void *, const void *, unsigned int);

void func_00107B98(struct func_00107B58_arg0 *arg0);                            /* extern */
s32 func_004AA558(s32);                             /* extern */

struct func_00107B58_arg0 {
    s32 unk0;
    s32 unk4;
};

s32 func_00107B58(struct func_00107B58_arg0 *arg0, s32 arg1) {
    s32 temp_v0;

    func_00107B98(arg0);
    temp_v0 = func_004AA558(arg1);
    arg0->unk4 = arg1;
    arg0->unk0 = temp_v0;
    return temp_v0;
}
/* native correction of src/startup/unit_00101400/func_00107B58.c: returns s32, the result of func_004AA558 left in $v0 on the PS2 */
