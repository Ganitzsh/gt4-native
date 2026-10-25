struct B { char p[0x1C]; char *tab; };
struct A { char p[4]; B *f4; };
struct E { char p[4]; short a; short b; short c; };
extern "C" int func_0042A220(A *self, int idx);
extern "C" void func_00428448(A *, int, int, int, int, int, float, float);

extern "C" void func_00428670(A *self, int arg1, int arg2, int arg3, float f0, float f1) {
    if (func_0042A220(self, arg1) != 0) {
        if (self->f4 != 0) {
            func_00428448(self, ((E *)(self->f4->tab + arg1 * 0x10))->a, ((E *)(self->f4->tab + arg1 * 0x10))->b, ((E *)(self->f4->tab + arg1 * 0x10))->c, arg2, arg3, f0, f1);
        }
    }
}
/* native correction of src/race/unit_004271E8/func_00428670.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_00428448 */
