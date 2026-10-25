#include "types.h"

extern "C" int func_0043A3C8(int arg0, int arg1);

typedef s32 (*VFn00435F18)(void *self, void *arg);

struct VEntry00435F18 {
    short delta;
    short index;
    VFn00435F18 fn;
};

struct Obj00435F18 {
    void *vt;
};

struct Unal00435F18 {
    s32 v;
} __attribute__((packed));

static inline s32 vcall00435F18(void *o, void *arg) {
    struct VEntry00435F18 *e = (struct VEntry00435F18 *)((char *)((struct Obj00435F18 *)o)->vt + 0x20);
    return e->fn((char *)o + e->delta, arg);
}

extern "C" s32 func_00435F18(void *arg0, void *arg1) {
    struct Unal00435F18 *src = (struct Unal00435F18 *)arg1;
    struct Unal00435F18 *dst = (struct Unal00435F18 *)((char *)arg0 + 4);
    dst->v = src->v;
    func_0043A3C8((int)arg1, 4);
    return vcall00435F18(arg0, arg1);
}
/* native correction of src/race/unit_004271E8/func_00435F18.cpp: returns the $v0 of the virtual call (vtable slot 4), which func_00437608 reads. */
