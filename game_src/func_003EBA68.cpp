#include "types.h"

struct Obj16 {
    char pad[0x10];
};

struct Self {
    char pad[0xD0];
    s32 fD0;
};

struct Out {
    s32 f0;
    float v[9];
    s32 f28;
    float f2C;
};

struct F9 {
    float v[9];
};

extern "C" void func_00427820(void *o);
extern "C" void GT4_Motion__MotionSetPlayer__set(void *o, int a, int b);
extern "C" int func_00427CD8(void *, void *, int, int, float, float);
extern "C" int func_00428298(void *o, void *out, int a, int b, float f0, float f1);

extern "C" void func_003EBA68(Self *self, int arg1, Out *out, float f) {
    Obj16 o;
    F9 tmp;
    float tmp2;
    if (self->fD0 == 0) {
        out->f0 = 0;
        out->f28 = 0;
        return;
    }
    func_00427820(&o);
    GT4_Motion__MotionSetPlayer__set(&o, 0, self->fD0);
    if (func_00427CD8(&o, &tmp, arg1, 0, f, 0) == 0) {
        out->f0 = 0;
    } else {
        out->f0 = 1;
        out->v[0] = tmp.v[0];
        out->v[1] = tmp.v[1];
        out->v[2] = tmp.v[2];
        out->v[3] = tmp.v[3];
        out->v[4] = tmp.v[4];
        out->v[5] = tmp.v[5];
        out->v[6] = tmp.v[6];
        out->v[7] = tmp.v[7];
        out->v[8] = tmp.v[8];
    }
    if (func_00428298(&o, &tmp2, arg1, 0, f, 0) == 0) {
        out->f28 = 0;
        return;
    }
    out->f28 = 1;
    out->f2C = tmp2;
}
/* native correction of src/race/unit_003EB740/func_003EBA68.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_00427CD8 */
