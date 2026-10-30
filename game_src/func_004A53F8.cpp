/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 004A53F8 (matched with VU0 asm): pushes the current matrix (shared vectors 28..31) on the
 * matrix stack, row 28 first, one row up per row (the original's VU0 microprogram 0x900 kept the stack in VU0 data
 * memory). */
#include "gt4_vec.h"
extern "C" void *memcpy(void *, const void *, unsigned int);

extern "C" void func_004A53F8(void) {
    unsigned int top = gt4_matrix_stack_top;
    for (int r = 28; r <= 31; ++r) {
        memcpy(gt4_matrix_stack_row(top), gt4_vec(r), 16);
        top = (top + 1) & 0xFFFF;
    }
    gt4_matrix_stack_top = (unsigned short)top;
}
