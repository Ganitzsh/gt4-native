#include "types.h"
#define NULL 0
void *memcpy(void *, const void *, unsigned int);

s32 func_0055C758(s32, s64);                        /* extern */
s32 func_0055D148(void *, s32);             /* extern */

struct func_0055D1C0_arg0 {
    s32 unk0;
    char pad4[0x4];
    s64 unk8;
};

s32 func_0055D1C0(struct func_0055D1C0_arg0 *arg0) {
    s32 r;
    if ((r = func_0055C758(arg0->unk0, arg0->unk8)) == 0) {
        r = func_0055D148(arg0, 1);
    }
    return r;
}
/* native correction of src/libstream/unit_005547E8/func_0055D1C0.c: returns the result of the last call (func_0055C758's when nonzero, else func_0055D148's) */
