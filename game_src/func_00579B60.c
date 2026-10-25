#include "types.h"
void *memcpy(void *, const void *, unsigned int);

s32 func_00579780(s32 arg0);                                /* extern */
s32 func_00579A48(s32);                             /* extern */
s32 func_00579BA0(s32, s32);                    /* extern */

long func_00579B60(s32 arg0) {
    s32 temp_s1;

    temp_s1 = func_00579780(arg0);
    return func_00579BA0(temp_s1, func_00579A48(arg0 + 4));
}
/* native correction of src/libstream/unit_005676B0/func_00579B60.c (gt4-decomp tools/argcheck.py fix --kind novalue --native): novalue func_00579B60 */
