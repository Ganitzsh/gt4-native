/* compiler: ee-gcc2.96 */
/* Native code for 00579B60 (matched; corrected for the native calling convention): converts a date record
 * (00579780: the day number of year/month/day, a Julian day count around 2.45 million) plus its time of day
 * (00579A48 on +4) to seconds with 00579BA0 (day * 86400 + seconds - 43200, computed in 64 bits by the
 * mult/mfhi/dsll32 at 0x579BB0-0x579BC8) and returns that 64-bit count. 00579BA0 is a tail call (j at
 * 0x579B98), so the whole $v0 is returned. The decomp's source declares 00579BA0 as returning s32; natively
 * the high word (about 0x31 << 32) was dropped. The declaration now returns long.
 * Earlier correction (argcheck --kind novalue): 00579B60 returns long, the value its callers read. */
#include "types.h"
void *memcpy(void *, const void *, unsigned int);

s32 func_00579780(s32 arg0);                                /* extern */
s32 func_00579A48(s32);                             /* extern */
long func_00579BA0(s32, s32);                    /* extern */

long func_00579B60(s32 arg0) {
    s32 temp_s1;

    temp_s1 = func_00579780(arg0);
    return func_00579BA0(temp_s1, func_00579A48(arg0 + 4));
}
/* native correction of src/libstream/unit_005676B0/func_00579B60.c (gt4-decomp tools/argcheck.py fix --kind novalue --native): novalue func_00579B60 */
