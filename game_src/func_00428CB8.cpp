extern "C" int func_00427778(int, int unused);
extern "C" void func_00428C00(void *, int, int, int, int, float, float);

extern "C" void func_00428CB8(void *self, int unused, int b, float c, int d, int e, float f) {
    int p = *(int *)((char *)self + 4);
    if (p != 0) {
        func_00428C00(self, func_00427778(p, unused), b, d, e, c, f);
    }
}
/* native correction of src/race/unit_004271E8/func_00428CB8.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_00428C00 */
