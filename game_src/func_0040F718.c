#include "types.h"
void *memcpy(void *, const void *, unsigned int);

f32 func_0040F670(f32);                             /* extern */
s32 func_004145C8(f32, f32);                    /* extern */

float func_0040F718(f32 fparg0, f32 fparg1) {
    return func_004145C8(fparg0, func_0040F670(fparg1));
}
/* native correction of src/race/unit_00408DB8/func_0040F718.c (gt4-decomp tools/argcheck.py fix --kind novalue --native): novalue func_0040F718 */
