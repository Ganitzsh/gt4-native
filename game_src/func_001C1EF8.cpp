/* Native version of 001C1EF8 (from its source): the sum of the bytes of an 8-bit plane (rows 16-aligned), without
   the prefetch hint (pref) in its inner loop. */
#include "types.h"

extern "C" s32 func_001C1EF8(u8 *arg0, s32 arg1, s32 arg2)
{
    s32 stride = (arg1 + 0xF) & ~0xF;
    s32 sum = 0;
    u8 *row = arg0;

    if (arg2 != 0) {
        do {
            s32 n = arg1;
            u8 *p = row;
            if (n != 0) {
                do {
                    sum += *p;
                    p++;
                    n--;
                } while (n != 0);
            }
            arg2 -= 1;
            row += stride;
        } while (arg2 != 0);
    }
    return sum;
}
