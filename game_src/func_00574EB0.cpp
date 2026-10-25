extern "C" void func_00576788(void *);
extern "C" void func_00574E30(void *arg0);
extern "C" long func_005767C0(void *arg0);

extern "C" long func_00574EB0(void *arg0) {
    void *s0 = arg0;
    func_00576788(arg0);
    func_00574E30(s0);
    return func_005767C0(s0);
}
/* native correction of src/libstream/unit_005676B0/func_00574EB0.cpp (gt4-decomp tools/argcheck.py fix --kind novalue --native): novalue func_00574EB0 */
