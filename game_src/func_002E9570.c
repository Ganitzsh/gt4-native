#include "types.h"
void *memcpy(void *, const void *, unsigned int);

s32 func_00266368(s32);                         /* extern */
s32 func_00266398(s32);                         /* extern */

struct func_002E9570_arg0 {
    char pad0[0xC4];
    s32 unkC4;
};

s32 func_002E9570(struct func_002E9570_arg0 *arg0, s32 arg1) {
    if (arg0->unkC4 != 0) {
        return func_00266398(arg1);
    }
    return func_00266368(arg1);
}
/* native correction of src/ui/unit_002E93C0/func_002E9570.c: returns the s32 of the tail call func_00266398 / func_00266368 that the caller reads from $v0. */
