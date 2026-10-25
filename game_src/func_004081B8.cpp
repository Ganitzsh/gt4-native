#include "types.h"

struct Obj {
    char p0[0xC];
    char *fC;
};

extern "C" s32 func_004080B8(Obj *arg0, s32 arg1);
extern "C" void func_00407DE8(char *, s32, s32, f32, f32);
extern "C" void func_00407EF0(char *p, s32 a);

extern "C" void func_004081B8(Obj *arg0, s32 arg1, s32 arg2, s32 arg3, s32 arg4, f32 fparg0, f32 fparg1) {
    if (func_004080B8(arg0, arg1) != 0) {
        s32 off = arg1 * 0x178;
        func_00407DE8(arg0->fC + off, arg3, arg4, fparg0, fparg1);
        func_00407EF0(arg0->fC + off, arg2);
    }
}
/* native correction of src/race/unit_004080B8/func_004081B8.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_00407DE8 */
