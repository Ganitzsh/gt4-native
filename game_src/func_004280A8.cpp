extern "C" int func_00427778(int, int unused);
extern "C" void func_00427FE8(void *, int, int, float, float, float, float, float);

extern "C" void func_004280A8(void *self, int unused, float b, float c, float d, float e, int f, float g) {
    int p = *(int *)((char *)self + 4);
    if (p != 0) {
        func_00427FE8(self, func_00427778(p, unused), f, b, c, d, e, g);
    }
}
/* native correction of src/race/unit_004271E8/func_004280A8.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_00427FE8 */
