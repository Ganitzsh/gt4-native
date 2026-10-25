struct Table;
struct A { int p0; Table *t; };
extern "C" int func_00427778(Table *, const char *) throw();
extern "C" void func_00428AD8(A *, int, int, int, float, float) throw();
extern "C" void func_00428B80(A *self, const char *name, float f0, int b, int c, float f1) {
    if (self->t) {
        func_00428AD8(self, func_00427778(self->t, name), b, c, f0, f1);
    }
}
/* native correction of src/race/unit_004271E8/func_00428B80.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_00428AD8 */
