// Imported from gustavocanepa/gt4-decomp @ de3d7edbead9e8a6e473b1e68b8ddf3b1f639fd1
// Original path: src/runtime/hArrayCompare/func_005C9970.c
#include "types.h"
void *memcpy(void *, const void *, unsigned int);

f32 func_001465D8();                                /* extern */

struct func_005C9970_arg0 {
    char pad0[0x174];
    s32 unk174;
};

f32 func_005C9970(struct func_005C9970_arg0 *arg0) {
    return (f32) arg0->unk174 / func_001465D8();
}
