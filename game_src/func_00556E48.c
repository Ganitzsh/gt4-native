#include "types.h"
s32 func_00557090(s32 a, s32 b);
void func_005570E0(s32 a, s32 b, char *c);
int func_00557728(s32 a, char *b);
s32 func_00556E48(s32 arg0, s32 arg1) {
    char sp[0x20];
    s32 r = func_00557090(arg0, arg1);
    if (r != 0) {
        func_005570E0(arg0, arg1, sp);
        return func_00557728(arg0, sp);
    }
    return r;
}
/* native correction of src/libstream/unit_005547E8/func_00556E48.c: returns what $v0 holds: func_00557728's mask, or 0 (func_00557090's zero result) when it skips. */
