#include "types.h"

struct Obj {
    char pad[0x4];
    u32 unk4;
};

extern "C" f32 MTRGeometry__getSeatX(Obj *arg0, s32 arg1);

extern "C" f32 MTRGeometry__GetSeatPosition_X(Obj *arg0) {
    return MTRGeometry__getSeatX(arg0, arg0->unk4 < 1);
}
/* native correction of src/race/MTRGeometry/MTRGeometry__GetSeatPosition_X.cpp: returns f32: the float MTRGeometry__getSeatX leaves in $f0 (it and this function were declared s32). */
