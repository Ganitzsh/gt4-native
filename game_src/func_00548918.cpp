/* compiler: ee-gcc2.96 */
/* Native copy of 00548918 (matched; corrected for the native calling convention): the current time, the
 * clock's 64-bit count from 005488A8 (its seconds plus 0x31510F5B40) plus the offset D_0064C8B0.
 * The matched source declares 005488A8 as returning s32: on the EE the whole 64-bit $v0 is still there and
 * the daddu at 0x548934 adds it; natively the high word (edx) was dropped, so the time lost 0x31 << 32. */
#include "types.h"

extern "C" s64 func_005488A8(int unused0);
extern "C" s64 D_0064C8B0;

extern "C" s64 func_00548918(int unused0) {
    return func_005488A8(unused0) + D_0064C8B0;
}
