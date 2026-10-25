#include "types.h"
extern "C" float D_00623A24;
extern "C" s32 D_00623A20;
extern "C" s32 D_00623A28;
extern "C" s32 D_00623A2C;
extern "C" float D_00623A30;
extern "C" void func_0045FFF8(int, int, int, float);
extern "C" void func_00461808(void *s, int a, float f, int b);
extern "C" int func_00461C00(int n);
extern "C" void func_00461D08(void);
extern "C" void func_00461C28(s32 arg0, s32 arg1, s32 arg2, float fparg0) {
    float t;
    s32 v;
    t = fparg0 * D_00623A24;
    func_00461D08();
    v = func_00461C00(arg0);
    switch (v) {
    case 0:
        D_00623A20 = 2;
        func_00461808((void *)arg0, arg1, t, arg2);
        break;
    case 1:
        D_00623A20 = v;
        func_0045FFF8(arg0 - 10, arg1, arg2, t);
        break;
    }
    D_00623A28 = arg0;
    D_00623A2C = arg1;
    D_00623A30 = fparg0;
}
/* native correction of src/engine/unit_00457510/func_00461C28.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_0045FFF8 */
