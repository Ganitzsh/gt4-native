/* Native version of 004A5400 (matched with VU0 asm): marks the matrix changed (scratchpad byte 0x70002000) and
 * pops the current matrix (shared vectors 28..31) from the matrix stack, one row down per row, row 31 first (the
 * original's VU0 microprogram 0x930). */
#include "gt4_vec.h"
#include "spr.h"
extern "C" void *memcpy(void *, const void *, unsigned int);

struct Spr {
    unsigned char dirty;
};

extern "C" void func_004A5400(void) {
    Spr *s = (Spr *)spr_base();
    s->dirty = 1;
    unsigned int top = gt4_matrix_stack_top;
    for (int r = 31; r >= 28; --r) {
        top = (top - 1) & 0xFFFF;
        memcpy(gt4_vec(r), gt4_matrix_stack_row(top), 16);
    }
    gt4_matrix_stack_top = (unsigned short)top;
}
