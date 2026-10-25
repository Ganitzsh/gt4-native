#include "types.h"

struct VEntry {
    s16 delta;
    s16 index;
    s32 (*fn)(void *, s32);
};

extern "C" s32 func_00250FC0(char *arg0, s32 arg1)
{
    s32 off = arg1 * 4 + 0x20;
    char *base = arg0 + 4;
    s32 *slot = (s32 *)(base + off);
    if (*slot == 0x200000) {
        char *p = *(char **)base;
        VEntry *e = (VEntry *)(p + 0x1D8);
        *slot = e->fn((char *)arg0 + e->delta, arg1);
    }
    return *slot;
}
/* native correction of src/ui/unit_002509E0/func_00250FC0.cpp: returns s32 *slot: the cached state, or the virtual call's result just stored there, both left in $v0 on the PS2 */
