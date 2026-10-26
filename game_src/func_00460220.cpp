/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native correction of src/engine/unit_00457510/func_00460220.cpp (argument order, argcheck.py orders): func_0045FFF8 is (index, select, mode, float volume); the decomp's (select, volume, mode) is the same EE call but matches only in that order. */
extern char D_00846640[];
extern "C" unsigned long func_00485BA0(const char *);
extern "C" int func_004859A0(void *, unsigned long);
extern "C" long func_0060A410(void *, int);
struct Instrument {
    void play(unsigned, int, float) throw() __asm__("func_0045FFF8");
};
extern "C" void func_00460220(const char *name, unsigned select, float volume, int mode) {
    int i = func_004859A0(D_00846640, func_00485BA0(name));
    if (i >= 0) {
        Instrument *p = (Instrument *)(int)func_0060A410(D_00846640, i);
        p->play(select, mode, volume);
    }
}
