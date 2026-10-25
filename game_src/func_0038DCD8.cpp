#include "types.h"

typedef int s32;

struct VEntry {
    s16 delta;
    s16 index;
    s32 (*fn)(void *);
};

struct ObjInner {
    VEntry *vtbl;
};

/* Layout backed by include/gt4/CarGeometry.h and include/gt4/NormalCarGeometry.h:
 *   CarGeometry: forwarding wrapper to inner geometry instance at 0x1C
 *   vtable slot 1 (0x8 / 8)
 */
struct Obj {
    char pad[0x1C];
    ObjInner *unk30;
};

extern "C" s32 CarGeometry__GetNumCrew(Obj *arg0) {
    ObjInner *a1 = arg0->unk30;
    VEntry *e = &a1->vtbl[1];

    return e->fn((char *)a1 + e->delta);
}
/* native correction of src/race/CarGeometry_NormalCarGeometry/CarGeometry__GetNumCrew.cpp: returns the s32 of the virtual GetNumCrew call (left in $v0). */
