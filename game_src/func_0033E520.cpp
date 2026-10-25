#include "types.h"

struct VEntry {
    s16 delta;
    s16 index;
    void *fn;
};

struct G {
    char pad[0x18];
    float f18;
    s32 i1c;
    char pad2[0x134 - 0x20];
};

extern G D_006184D0[];

extern "C" void func_00105280(void *, s32);
extern "C" void *func_00105378(void *, void *);
extern "C" void func_001056A0(void *);
extern "C" void func_001057F8(void *);
extern "C" void *func_00105AD8(void *, s32, s32, s32);
extern "C" void func_00105C48(void *, void *, s32, s32, float, float, float, float);
extern "C" int func_003993D0(void *);
extern "C" void func_00399490(void *, float, float);
extern "C" void func_003994C0(void *);
extern "C" void func_00399998(void *, void *, void *, void *, void *, s32, float, float, float, float);
extern "C" void func_004A1638(s32);
extern "C" void func_004AB040(s32);

typedef s32 (*TestFn)(char *);

extern "C" void func_0033E520(char *arg0, void *arg1, s32 arg2, void *arg3, void *arg4, float fp0, float fp1, float fp2) {
    char sp[0xC0];
    char obj[0x40];
    char *s1 = *(char **)(*(char **)(*(char **)(*(char **)(arg0 + 0x6C) + 0x80) + 4) + 0x80);
    func_001057F8(arg1);
    float f20 = *(float *)(s1 + 0x38);
    func_00105AD8(arg4, arg2, 0, 0);
    VEntry *e = (VEntry *)(*(char **)(arg0 + 0x64) + 0x460);
    if (((TestFn)e->fn)(arg0 + e->delta) != 0) {
        func_003993D0(sp);
        func_001056A0(arg3);
        func_004A1638(5);
        func_004AB040(6);
        G *g1 = &D_006184D0[1];
        func_00105378(obj, g1);
        *(s32 *)(obj + 4) = 1;
        func_00399490(sp, fp0, 0x1.553f7c0000000p+0f);
        *(float *)(sp + 8) = fp1;
        *(float *)(sp + 0x10) = fp2;
        func_003994C0(sp);
        s32 ia = (g1 - 1)->i1c;
        float fa = (g1 - 1)->f18;
        func_00399998(sp, arg3, arg1, arg4, obj, ia, fa, f20, f20, f20);
        func_00105280(obj, 2);
        func_00105280(sp + 0x2C, 2);
    } else {
        func_004AB040(5);
        func_00105C48(arg3, arg1, D_006184D0[0].i1c, 0, D_006184D0[0].f18, f20, f20, f20);
    }
}
/* native correction of src/race/RaceBGMPS2_RacePS2Base/func_0033E520.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_00399998 */
