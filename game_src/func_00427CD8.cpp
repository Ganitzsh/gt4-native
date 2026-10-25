struct B { char p[0x1C]; char *tab; };
struct A { char p[4]; B *f4; };
struct E { char p[4]; short a; short b; short c; };
extern "C" int func_0042A220(A *self, int idx);
extern "C" int func_00427B70(A *, int, int, float, float, int, int, int);

extern "C" int func_00427CD8(A *self, int arg1, int arg2, int arg3, float f0, float f1) {
    int r = func_0042A220(self, arg2);
    if (r != 0) {
        r = 0;
        if (self->f4 != 0) {
            return func_00427B70(self, arg1, ((E *)(self->f4->tab + arg2 * 0x10))->a, f0, f1, ((E *)(self->f4->tab + arg2 * 0x10))->b, ((E *)(self->f4->tab + arg2 * 0x10))->c, arg3);
        }
    }
    return r;
}
/* native correction of src/race/unit_004271E8/func_00427CD8.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_00427B70 */
