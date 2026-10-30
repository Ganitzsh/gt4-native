/* Native version of 001C1F50 (from its source): the sum of the bytes of a rectangle of an 8-bit plane (rows
   16-aligned), without the prefetch hint (pref) in its inner loop. */
#include "types.h"

extern "C" s32 func_001C1F50(u8 *arg0, s32 arg1, s32 arg2, s32 arg3, s32 arg4, s32 arg5, s32 arg6)
{
    s32 stride = (arg1 + 0xF) & ~0xF;
    s32 sum = 0;
    u8 *row = arg0 + (arg4 * stride + arg3);

    if (arg6 != 0) {
        do {
            s32 n = arg5;
            u8 *p = row;
            if (n != 0) {
                do {
                    sum += *p;
                    p++;
                    n--;
                } while (n != 0);
            }
            arg6 -= 1;
            row += stride;
        } while (arg6 != 0);
    }
    return sum;
}
