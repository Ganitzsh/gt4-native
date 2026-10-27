/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native code for 0022BC20 (unmatched; from the decomp's draft, corrected against the original): when n > 0,
 * builds a handle in buf0 from arg1 (func_0022AD20), calls func_0022F6A0 on it, then for each of the n
 * entries of args builds a temporary in buf1 (func_00232C78), passes it to func_0022F890 and destroys it
 * (func_00232C20), then calls func_0022F948 on the handle and finally destroys the handle (func_0022ACC8).
 * The draft destroyed the handle (func_0022ACC8(buf0, 2)) right after func_0022F6A0, before the loop and
 * func_0022F948 use it; the original calls it last (jal func_22ACC8 after jal func_22F948, at 0x22BCB8). */
#include "types.h"

extern "C" void func_0022ACC8(void *arg0, int arg1) throw();
extern "C" void func_0022AD20(void *arg0, void *arg1) throw();
extern "C" void func_0022F6A0(s32 arg0) throw();
extern "C" void func_0022F890(s32 arg0, s32 arg1) throw();
extern "C" void func_0022F948(s32 arg0) throw();
extern "C" void func_00232C20(void *arg0, int arg1) throw();
extern "C" void func_00232C78(void *arg0, void *arg1) throw();

extern "C" void func_0022BC20(void *arg0, void *arg1, s32 n, s32 *args) {
    s32 buf0[4];
    s32 buf1[4];
    s32 *p1;

    if (n > 0) {
        p1 = buf1;
        func_0022AD20(buf0, arg1);
        func_0022F6A0(buf0[0]);
        for (s32 i = 0; i < n; i++) {
            s32 obj = buf0[0];
            func_00232C78(p1, &args[i]);
            func_0022F890(obj, p1[0]);
            func_00232C20(buf1, 2);
        }
        func_0022F948(buf0[0]);
        func_0022ACC8(buf0, 2);
    }
}
