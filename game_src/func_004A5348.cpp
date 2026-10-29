/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 004A5348 (draft with VU0 asm): makes matrix set `index` (0, 1, else 2) current. The current
 * matrix (shared vectors 28..31) and the matrix stack pointer (vi13) of the set in use go to its slot in the
 * scratchpad (0x70002ED0 + 0x40 k, vi13 at 0x70002F90 + 2 k), the other set's come back from its slot. Nothing
 * when it is current already. */
#include "gt4_vec.h"
#include "spr.h"
extern "C" void *memcpy(void *, const void *, unsigned int);
typedef unsigned char u8;
typedef unsigned short u16;

/* The scratchpad's three saved VU0 matrix sets (vf28-vf31 and vi13). */
struct Spr {
    char pad0;
    u8 current;
    char pad2[0xECE];
    float matrix[3][4][4];
    u16 vi13[3];
};

extern "C" void func_004A5348(int index) {
    Spr *s = (Spr *)spr_base();
    const int current = s->current;
    if (current == index)
        return;
    const int from = current == 0 ? 0 : current == 1 ? 1 : 2;
    const int to = index == 0 ? 0 : index == 1 ? 1 : 2;
    s->vi13[from] = (u16)__gt4_vu0.vi[13];
    for (int r = 0; r < 4; ++r)
        memcpy(s->matrix[from][r], gt4_vec(28 + r), 16);
    for (int r = 0; r < 4; ++r)
        memcpy(gt4_vec(28 + r), s->matrix[to][r], 16);
    __gt4_vu0.vi[13] = s->vi13[to];
    s->current = (u8)index;
}
