#include "types.h"

struct VEntry {
    s16 delta;
    s16 index;
    float (*fn)(void *);
};

struct Sub {
    VEntry *vtbl;
};

/* Layout backed by include/gt4/CarGeometry.h and include/gt4/NormalCarGeometry.h:
 *   CarGeometry: forwarding wrapper to inner geometry instance at 0x1C
 *   vtable slot 4 (0x20 / 8)
 */
struct Obj {
    u8 pad0[0x1C];
    Sub *unk1C;
};

extern "C" float CarGeometry__GetSeatPosition_Z(Obj *arg0, float unusedf0) {
    Sub *s = arg0->unk1C;
    VEntry *e = &s->vtbl[4];

    return e->fn((char *)s + e->delta);
}
/* native correction of src/race/CarGeometry_NormalCarGeometry/CarGeometry__GetSeatPosition_Z.cpp: returns the float ($f0) that the virtual call leaves, which the callers read. */
