#include "types.h"

struct Obj1 {
    char pad[0xDC];
    s32 unkDC;
};

struct Obj0 {
    Obj1 *first;
};

struct Stk {
    s32 w0;
    s32 w4;
    s32 w8;
    s32 wC;
};

extern char RaceDisplayGeneralTimeEvent__vtable[];

extern "C" s32 func_0034C190(s32 arg0, s32 arg1);
extern "C" s32 func_0035E3F8(void *a0, void *a1, s32 a2);
extern "C" void RaceDisplayCheckPointEvent__structor_0(void *p);
extern "C" s32 RaceDisplayEventBase__put(void *o, void *t);
extern "C" void *RaceDisplayDiffEvent__structor_0(void *p);
extern "C" void func_003A3660(void *p, u32 v);
extern "C" s32 func_003C0C60(void *a0, s32 a1, s32 a2, s32 a3);

extern "C" s32 RaceDisplayGeneralTimeEvent__structor_0(Obj0 *arg0, s32 arg1, s32 arg2, s32 arg3, s32 arg4, s32 arg5, s32 arg6) {
    Stk st;
    Stk st2;
    Stk *p2;
    s32 v;
    Obj1 *s1;
    s32 s4;
    u8 *q;
    s32 r;

    v = func_0034C190((s32)arg0, arg1);
    q = (u8 *)v + 0x104;
    if (q[0x441] != 0) return q[0x441];
    if (q[0x632] != 0) return q[0x632];
    {
        s1 = arg0->first;
        r = func_0035E3F8(s1, arg0, v);
        s4 = r == 0;
        if (s1->unkDC == 0) {
            RaceDisplayCheckPointEvent__structor_0(&st);
            st.w0 = arg1;
            st.w8 = arg2;
            st.wC = arg3;
            RaceDisplayEventBase__put(&st, s1);
            p2 = &st2;
            RaceDisplayDiffEvent__structor_0(p2);
            p2->w4 = (s32)RaceDisplayGeneralTimeEvent__vtable;
            p2->w0 = arg1;
            func_003A3660(p2, arg5);
            r = RaceDisplayEventBase__put(p2, s1);
        }
        if (arg6 == 0 && s4 != 0) {
            r = func_003C0C60(s1, arg1, arg3, arg5);
        }
    }
    return r;
}
/* native correction of src/race/unit_0034E9A0/RaceDisplayGeneralTimeEvent__structor_0.cpp: returns what $v0 holds at each PS2 return: the nonzero flag byte q[0x441] / q[0x632], else the last of func_0035E3F8, the second RaceDisplayEventBase__put, func_003C0C60 (declarations made s32). */
