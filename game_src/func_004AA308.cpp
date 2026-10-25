#include "types.h"

extern u32 D_0062A03C;

extern "C" u32 func_004AA308(void) {
    D_0062A03C = (u64)D_0062A03C * 0x5D588B65 + 1;
    return D_0062A03C;
}
/* native correction of src/network/unit_00494578/func_004AA308.cpp: returns the new D_0062A03C, which $v0 holds at jr $ra */
