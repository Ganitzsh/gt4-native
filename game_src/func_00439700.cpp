#include "types.h"

extern "C" s32 func_00560690(void *arg0, s32 arg1);

struct VEntry {
    s16 delta;
    s16 pad;
    s32 (*fn)(void *, s32);
};

struct Obj {
    VEntry *vtbl;
    u8 pad4[4];
};

extern "C" s32 func_00439700(Obj *self, s32 arg1) {
    func_00560690((void *) &self->pad4, arg1);
    VEntry *entry = self->vtbl + 4;
    s16 delta = entry->delta;
    s32 (*fn)(void *, s32) = entry->fn;
    return fn((void *) ((char *) self + delta), arg1);
}
/* native correction of src/race/unit_004271E8/func_00439700.cpp: returns the $v0 of the virtual call (vtable slot 4), which func_004374A8 reads. */
