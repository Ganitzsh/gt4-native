#include "types.h"

struct Obj003C0098 {
    char pad0[0x80];
    s32 unk80;
};

extern "C" f32 RaceCourse__getCheckPoint(s32 arg0, s32 arg1, s32 arg2);

extern "C" f32 func_003C0098(struct Obj003C0098 *arg0, s32 arg1) {
    return RaceCourse__getCheckPoint(arg0->unk80, arg1, 1);
}
/* native correction of src/race/unit_003BFAB0/func_003C0098.cpp: returns the f32 ($f0) of RaceCourse__getCheckPoint (a float on the PS2; callers such as func_00122D38 read $f0), not an s32. */
