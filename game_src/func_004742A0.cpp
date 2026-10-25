#include "types.h"
struct Ptr { char p[0xE8]; s32 fE8; };
struct Self { char p0[0x1C]; f32 f1C; short s20; char p22[0x30 - 0x22]; void *f30; };
struct L {
    char a[0x5008];
    char b[0x6E0C - 0x5008];
    Ptr *c;
    char pad[0x6E90 - 0x6E10];
    s32 d0[4];
    s32 d1[4];
    s32 d2[4];
};
extern "C" void func_00474368(void *, s32);
extern "C" void func_00474568(void *, void *, f32);
extern "C" int func_004791C0(void *, s32, void *, s32, s32, f32);
extern "C" void *func_00605570(void *, void *, void *, void *);
extern "C" void func_00605618(void *, void *);
extern "C" void func_004742A0(Self *self, s32 a1, s32 a2, f32 f) {
    L sp;
    f32 t = f * self->f1C;
    if (!((f32)self->s20 <= t)) {
        func_00474368(self, a1);
        func_004791C0(&sp, a1, self, a2, 0, 0.0f);
        func_00474568(self->f30, &sp, t);
        sp.d1[1] = sp.c->fE8;
        sp.d2[1] = (s32)sp.c;
        func_00605570(sp.d0, sp.b, sp.d1, sp.d2);
        func_00605618(sp.b, sp.c);
    }
}
/* native correction of src/engine/unit_0046A050/func_004742A0.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_004791C0 */
