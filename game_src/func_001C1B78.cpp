/* Native version of 001C1B78 (from its source): the movie player's nearest-neighbour resample of an 8-bit
   plane, without the prefetch hint (pref) in its inner loop. */
#include "types.h"

extern "C" void func_001C1B78(u8 *dst, u32 width, u32 rows, u32 div, u32 lim, u8 *src, u32 pitch, u32 unused, u32 step, u32 step2)
{
    u32 k = 15;
    u32 mask = ~k;
    u32 pp = pitch + k;
    u32 ww = width + k;
    u32 aw = ww & mask;
    u32 ap = pp & mask;
    u32 acc2 = 0;
    u8 *row = src;
    u8 *d = dst;
    if (rows != 0) {
        do {
            u8 *p = d;
            u8 *s = row;
            u32 acc = 0;
            u32 n = width;
            if (n != 0) {
                do {
                    *p = *s;
                    p += 1;
                    acc += step;
                    if (acc >= div) {
                        do {
                            acc -= div;
                            s += 1;
                        } while (acc >= div);
                    }
                    n -= 1;
                } while (n != 0);
            }
            acc2 += step2;
            d += aw;
            if (acc2 >= lim) {
                do {
                    acc2 -= lim;
                    row += ap;
                } while (acc2 >= lim);
            }
            rows -= 1;
        } while (rows != 0);
    }
}
