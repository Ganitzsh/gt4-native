#include "types.h"

struct VEntry_0038E008 {
    s16 delta;
    s16 index;
    float (*fn)(void *, float);
};

struct Inner_0038E008 {
    VEntry_0038E008 *vtbl;
};

/* Layout backed by include/gt4/CarGeometry.h and include/gt4/NormalCarGeometry.h:
 *   CarGeometry: forwarding wrapper to inner geometry instance at 0x1C
 *   vtable slot 18 (0x90 / 8)
 */
struct Obj_0038E008 {
    char pad[0x1C];
    Inner_0038E008 *unk1C;
};

extern "C" float CarGeometry__GetShiftPosition_X(struct Obj_0038E008 *arg0, float unusedf0) {
    Inner_0038E008 *a1 = arg0->unk1C;
    VEntry_0038E008 *e = &a1->vtbl[18];

    return e->fn((char *)a1 + e->delta, unusedf0);
}
/* native correction of src/race/CarGeometry_NormalCarGeometry/CarGeometry__GetShiftPosition_X.cpp: returns the float ($f0) that the virtual call leaves, which the callers read. */
