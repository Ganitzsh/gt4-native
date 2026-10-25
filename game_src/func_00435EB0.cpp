#include "types.h"

extern "C" int func_0043A398(void *arg0, s32 arg1);

typedef s32 (*VFn00435EB0)(void *self, void *arg);

struct VEntry00435EB0 {
    short delta;
    short index;
    VFn00435EB0 fn;
};

struct Obj00435EB0 {
    void *vt;
};

struct Unal00435EB0 {
    s32 v;
} __attribute__((packed));

static inline s32 vcall00435EB0(void *o, void *arg) {
    struct VEntry00435EB0 *e = (struct VEntry00435EB0 *)((char *)((struct Obj00435EB0 *)o)->vt + 0x20);
    return e->fn((char *)o + e->delta, arg);
}

extern "C" s32 func_00435EB0(void *arg0, void *arg1) {
    struct Unal00435EB0 *src = (struct Unal00435EB0 *)((char *)arg0 + 4);
    struct Unal00435EB0 *dst = (struct Unal00435EB0 *)arg1;
    dst->v = src->v;
    func_0043A398(arg1, 4);
    return vcall00435EB0(arg0, arg1);
}
/* native correction of src/race/unit_004271E8/func_00435EB0.cpp: returns the $v0 of the virtual call (vtable slot 4), which func_004374A8 reads. */
