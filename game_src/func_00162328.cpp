/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native code for 00162328 MGame__get_car_garage_2p (matched; corrected for the native calling convention):
 * the second player's twin of MGame__get_car_garage (record +0x3A900). The matched source declares 004454C0
 * (the stored s64 car id) as returning s32; on the EE the test at 0x1623DC compares the whole 64-bit $v0
 * with -1. Natively only the low word was compared; the declaration now returns s64. */
typedef int s32;
typedef long long s64;
struct Q { char pad[0x10]; s32 f10; };
struct H { Q *p; s32 pad[3]; };
extern "C" void func_0013BD68(void *, int);
extern "C" void func_0013BDC0(void *, void *);
extern "C" int func_00147D80(void *);
extern "C" void func_00148140(void *, s32);
extern "C" void func_0015F388(void *, int) throw();
extern "C" void func_0015F3E0(void *, s32 unused) throw();
extern "C" void func_00309348(void *, void *);
extern "C" void func_00309378(void *, s32);
extern "C" void func_003285A8(s32);
extern "C" void func_003285F8(s32);
extern "C" void func_0043DE40(s32, int);
extern "C" void func_0043E240(s32);
extern "C" s32 func_00441248(s32);
extern "C" s64 func_004454C0(s32);

extern "C" void MGame__get_car_garage_2p(s32 *out, s32 unused, s32 n, s32 *arg3) {
    H h;
    func_0015F3E0(&h, unused);
    s32 v = h.p->f10 + 0x3A900;
    func_0015F388(&h, 2);
    if (n > 0) {
        if (*arg3 == 0) {
            func_0043E240(v);
        } else {
            func_0013BDC0(&h, arg3);
            func_0043DE40(v, func_00147D80(h.p));
            func_0013BD68(&h, 2);
        }
        return;
    }
    if (func_004454C0(func_00441248(v)) != -1) {
        func_00148140(&h, v);
        if (out != (s32 *)&h) {
            s32 nv = (s32)h.p;
            if (nv) func_003285A8(nv);
            if (*out) func_003285F8(*out);
            *out = nv;
        }
        func_0013BD68(&h, 2);
        return;
    }
    s32 zero = 0;
    func_00309348(&h, &zero);
    if (out != (s32 *)&h) {
        s32 nv = (s32)h.p;
        if (nv) func_003285A8(nv);
        if (*out) func_003285F8(*out);
        *out = nv;
    }
    func_00309378(&h, 2);
}
