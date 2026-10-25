#include "types.h"

extern char RaceDisplayTimeDiffEvent__vtable[];

extern "C" void *func_0034C190(s32 arg0, s32 arg1);
extern "C" int RaceDisplayEventBase__put(void *o, void *t);
extern "C" void *RaceDisplayDiffEvent__structor_0(void *arg0);
extern "C" void RaceDisplayTimeDiffEvent__setTimeDiff(void *s, s32 x);

struct Auto_unk84 {
    char pad0[0x70];
    s32 unk70;
};

struct Auto_arg0 {
    char pad0[0x84];
    struct Auto_unk84 *unk84;
    char pad88[0x54];
    s32 unkDC;
};

extern "C" s32 RaceDisplayTimeDiffEvent__structor_0(struct Auto_arg0 *arg0, s32 arg1, s32 arg2) {
    s32 sp[4];
    void *e = func_0034C190(arg0->unk84->unk70, arg1);
    if (*((u8 *)e + 0x736) == 0) {
        if (arg0->unkDC == 0) {
            RaceDisplayDiffEvent__structor_0(sp);
            sp[1] = (s32)RaceDisplayTimeDiffEvent__vtable;
            sp[0] = arg1;
            RaceDisplayTimeDiffEvent__setTimeDiff(sp, arg2);
            return RaceDisplayEventBase__put(sp, arg0);
        }
        return arg0->unkDC;
    }
    return (s32)e;
}
/* native correction of src/race/unit_0034E9A0/RaceDisplayTimeDiffEvent__structor_0.cpp: returns what $v0 holds at each PS2 return: the func_0034C190 pointer, the nonzero unkDC, or RaceDisplayEventBase__put's int (its declaration made int). */
