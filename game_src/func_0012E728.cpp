/* compiler: ee-gcc2.96 */
/* Native code for 0012E728 MCarData__GetCarNumericCode (matched; corrected for the native calling
 * convention): the script method that returns a car's numeric code as a string: the 64-bit database id of
 * the car named by argument 0 (00447BB8: (kind << 32) | row) formatted with "%lx" (D_0068E828) by 0057DA20.
 * The matched source declares 00447BB8 as returning s32; on the EE the whole $v0 is kept (daddu $s0, $v0 at
 * 0x12E770) and passed in $a2 to the sprintf at 0x12E784, whose %lx reads a 64-bit long. Natively the high
 * word (the kind) was dropped and %lx read 8 bytes from a 4-byte argument; the id is now s64. */
#include "types.h"

struct Rep {
    s32 len;
    s32 cap;
    s32 ref;
    s32 sel;
};

struct Str {
    char *p;
    char pad[0xC];
};

struct S00659988 {
    const char *name;
};

extern Rep D_00659FA8;
extern char D_0068E828[];
extern "C" void func_00312370(void *, s32);
extern "C" s32 func_00314AD8(s32);
extern "C" s64 func_00447BB8(s32);
extern "C" void func_00312318(void *, s32);
extern "C" void * func_0057DA20(void *, void *, s64);
extern "C" char * func_005C2560(Rep *);
extern "C" s32 func_0057F260(void *);
extern "C" void * func_005C2630(void *, s32, s32, void *, s32);
extern "C" void func_00314B20(void *, void *);
extern "C" void func_003285A8(s32);
extern "C" void func_003285F8(s32);
extern "C" struct S00659988 * func_005C11A8(void);
extern "C" void func_00326798(void *, s32, s32, const char *);

static inline void str_release(Str *s) {
    Rep *q = (Rep *)(s->p - 0x10);
    if (--q->ref == 0) {
        s32 size = q->cap + 0x10;
        func_00326798(q, size, 4, func_005C11A8()->name);
    }
}

extern "C" void MCarData__GetCarNumericCode(s32 *arg0, void *arg1, s32 arg2) {
    s32 buf0[4];
    s32 buf1[4];
    s32 buf2[4];
    s32 buf3[4];
    Str s4;
    s64 v_s0;
    s32 *p_s1;
    Str *p_s3;
    s32 newVal;
    s32 oldVal;
    if ((s32)arg1 > 0) {
        func_00312370(buf0, arg2);
        v_s0 = func_00447BB8(func_00314AD8(buf0[0]));
        func_00312318(buf0, 0x2);
        p_s1 = buf1;
        func_0057DA20(p_s1, &D_0068E828, v_s0);
        p_s3 = &s4;
        {
            Rep *r = &D_00659FA8;
            char *d;
            if (r->sel != 0) {
                d = func_005C2560(r);
            } else {
                d = (char *)(r + 1);
                r->ref++;
            }
            p_s3->p = d;
        }
        func_005C2630(p_s3, 0, -0x1, p_s1, func_0057F260(p_s1));
        func_00314B20(buf0, p_s3);
        if (arg0 != buf0) {
            newVal = buf0[0];
            if (newVal != 0) {
                func_003285A8(newVal);
            }
            oldVal = *arg0;
            if (oldVal != 0) {
                func_003285F8(oldVal);
            }
            *arg0 = newVal;
        }
        func_00312318(buf0, 0x2);
        str_release(p_s3);
    }
}
