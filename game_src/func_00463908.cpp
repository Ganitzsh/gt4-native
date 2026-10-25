#include "types.h"

struct List {
    char pad0[0xC];
    s32 unkC;
    s32 unk10;
    s32 unk14;
};

extern "C" s32 func_00463908(List *arg0, s32 *arg1) {
    *arg1 = arg0->unkC;
    arg0->unkC = (s32)arg1;
    arg0->unk14 = arg0->unk14 + 1;
    arg0->unk10 = arg0->unk10 - 1;
    return arg0->unk14;
}
/* native correction of src/engine/unit_00463840/func_00463908.cpp: returns the incremented unk14, the value left in $v0 on the PS2 */
