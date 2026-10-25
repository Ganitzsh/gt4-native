extern "C" void func_003335F0(void *self, int a);
extern "C" void func_003335F8(void *, int, int, int, int, int, float);

extern "C" void func_00333560(void *self, int a, int b, int c, int d, int e, float g, int f) {
    func_003335F0(self, a);
    func_003335F8(self, b, c, d, e, f, g);
}
/* native correction of src/race/unit_00330800/func_00333560.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_003335F8 */
