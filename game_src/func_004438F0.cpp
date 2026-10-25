struct T { int f0; char name[0x12c]; };
extern "C" int func_00443CD0(void *self, char *buf, char *ext);
extern "C" int func_00443DC0(void *self, int i);
extern "C" void func_00448F18(void *o);
extern "C" int func_00448FF8(void *o, char *buf);
extern "C" int func_00449070(void *o, char *buf, int a, int b);
extern "C" int func_00575E60(int a, int b);
extern "C" void func_005A5DC8(char *d, char *s);
extern "C" char *func_005A609C(char *d, char *s);
extern "C" void *exception__structor_0(int n);
extern char *D_006234B0[];
extern char *D_00623580[];
extern char D_006A6D20[];

extern "C" int func_004438F0(char *self, int idx) {
    char buf[0x100];
    func_005A609C(buf, self + 8);
    T *t = (T *)(self + 4);
    func_005A5DC8(buf, D_006234B0[idx]);
    int off = idx * 4;
    off += 0x130;
    void **slot = (void **)((char *)t + off);
    void *o = exception__structor_0(0x6C);
    func_00448F18(o);
    *slot = o;
    if (func_00443DC0(self, idx) != 0) {
        int a = func_00443CD0(self, buf, D_006A6D20);
        int b = func_00575E60(0x40, a);
        func_005A5DC8(buf, D_00623580[t->f0]);
        func_005A5DC8(buf, D_006A6D20);
        return func_00449070(*slot, buf, b, a);
    }
    func_005A5DC8(buf, D_006A6D20);
    return func_00448FF8(*slot, buf);
}
/* native correction of src/race/unit_004271E8/func_004438F0.cpp: returns the result of func_00449070 / func_00448FF8 (the last call on each path, left in $v0 on the PS2) */
