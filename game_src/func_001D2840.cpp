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

struct Obj {
    char pad[0x30];
    ObjInner *unk30;
};

extern "C" s32 func_001D2840(Obj *arg0) {
    ObjInner *a1 = arg0->unk30;
    VEntry *e = &a1->vtbl[3];

    return e->fn((char *)a1 + e->delta);
}
/* native correction of src/media/unit_001C4BE0/func_001D2840.cpp: returns s32, the result of the virtual call left in $v0 on the PS2 */
