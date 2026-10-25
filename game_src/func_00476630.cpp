#include "types.h"

extern "C" s16 *func_005C15B0(s32 arg0);

extern "C" s16 *func_00476630(s32 arg0) {
    s16 *p = func_005C15B0(arg0 + 4);
    *p = 0;
    return p;
}
/* native correction of src/engine/unit_0046A050/func_00476630.cpp: returns the pointer from func_005C15B0, left in $v0 on the PS2 */
