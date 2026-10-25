#include "types.h"

struct Obj {
    char pad[0x4];
    u32 unk4;
};

extern "C" f32 NormalCarGeometry__getSeatX(Obj *arg0, s32 arg1, f32);

extern "C" f32 NormalCarGeometry__GetSeatPosition_X(Obj *arg0, f32 fparg1) {
    return NormalCarGeometry__getSeatX(arg0, arg0->unk4 < 1, fparg1);
}
/* native correction of src/race/CarGeometry_NormalCarGeometry/NormalCarGeometry__GetSeatPosition_X.cpp: returns f32: the float NormalCarGeometry__getSeatX leaves in $f0 (it and this function were declared s32). */
