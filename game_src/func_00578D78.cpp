extern "C" void *func_0057B2E8(void *, int, int, int);
extern "C" void func_00576090(void *arg0);
extern "C" char D_00689E80[];
extern "C" char D_00689EF8[];

struct func_00578D78_arg0 {
    char pad0[0x18];
    void *unk18;
};

extern "C" void *func_00578D78(void *arg0, int arg1, int arg2, int arg3)
{
    func_0057B2E8(arg0, arg1, arg2, arg3);
    ((struct func_00578D78_arg0 *)arg0)->unk18 = D_00689E80;
    func_00576090((char *)arg0 + 0x1C);
    return D_00689EF8; /* $v0 left by func_0057B2E8; func_00576090 and func_00576050 never write it */
}
/* native correction of src/libstream/unit_005676B0/func_00578D78.cpp: returns D_00689EF8, the value $v0 holds on the PS2 (left by func_0057B2E8, untouched by func_00576090/func_00576050), and calls func_00576090 as void */
