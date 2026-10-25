#include "types.h"

struct VEntry {
    s16 delta;
    s16 index;
    float (*fn)(void *, float);
};

struct ObjInner {
    VEntry *vtbl;
};

/* Layout backed by include/gt4/CarGeometry.h and include/gt4/NormalCarGeometry.h:
 *   CarGeometry: forwarding wrapper to inner geometry instance at 0x1C
 *   vtable slot 2 (0x10 / 8)
 */
struct Obj {
    char pad[0x1C];
    ObjInner *unk1C;
};

extern "C" float CarGeometry__GetSeatPosition_X(Obj *arg0, float unusedf0) {
    ObjInner *a1 = arg0->unk1C;
    VEntry *e = &a1->vtbl[2];

    return e->fn((char *)a1 + e->delta, unusedf0);
}
/* native correction of src/race/CarGeometry_NormalCarGeometry/CarGeometry__GetSeatPosition_X.cpp: returns the float of the virtual GetSeatPosition_X call (left in $f0) and passes it the float argument, which stays in $f12 on the PS2 (NormalCarGeometry::getSeatX reads it). */
