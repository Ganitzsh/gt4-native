#include "types.h"

struct Obj {
    int m0;
    void *items;
    int count;
};

extern "C" u64 func_00447C78(void *x);
extern "C" void *func_005A3008(const void *key, const void *base, int n, int size, int (*cmp)(const void *, const void *));
extern "C" int func_00433BE8(const void *a, const void *b);
extern "C" void *func_00433AA0(Obj *o, void *x);

extern "C" void *func_00433C10(Obj *o, void *x) {
    u64 key = func_00447C78(x);
    void *r = func_005A3008(&key, o->items, o->count, 0x238, func_00433BE8);
    if (r == 0)
        r = func_00433AA0(o, x);
    return r;
}
/* native correction of src/race/unit_004271E8/func_00433C10.cpp: returns the record pointer ($v0): the bsearch (func_005A3008) result, or when that is 0 the row func_00433AA0 returns, which the callers read. */
