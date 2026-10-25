struct Obj1 {
    char pad[0x2DC];
    int bufIndex;
};

extern "C" int func_00459470(void *out, void *a, void *b, float t, int unused0);

extern "C" void *func_0045AB58(void *arg0, Obj1 *arg1, int arg2, float t) {
    char *v1 = (char *)arg1;
    int v0 = arg2 * 0x34;
    int b = arg1->bufIndex;
    int a1 = 1 - b;
    char *p0 = (b * 0x16C) + v1;
    p0 = p0 + v0;
    char *p1 = (a1 * 0x16C) + v1;
    p0 = p0 + 0x4C;
    p1 = p1 + v0;
    p1 = p1 + 0x4C;
    func_00459470(arg0, p1, p0, t, 0x16C);
    return arg0;
}
/* native correction of src/engine/unit_00457510/func_0045AB58.cpp: takes the blend factor t (passed through in $f12 on the PS2) and passes it to func_00459470 before the int, in the callee's order */
