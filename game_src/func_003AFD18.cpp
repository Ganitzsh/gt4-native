/* compiler: ee-gcc2.96 */
/* Native copy of the race HUD's float x alignment (003AFD18, matched) for any display aspect
 * (docs/WIDESCREEN.md): an anchored element (flags bit 2; bit 3 the right side) moves by
 * RaceDisplayUtil::wideOffset's amount (game_src/func_003AFDD8.c) instead of the 16:9 mode's 96
 * units; the low two bits then align it (0 left, 1 right, else centred). */
#include "types.h"

extern "C" f32 RaceDisplayUtil__wideOffset(s32 flags);

extern "C" f32 func_003AFD18(int flags, f32 x, f32 width) {
    x += RaceDisplayUtil__wideOffset(flags);
    switch (flags & 3) {
    case 0:
        return x;
    case 1:
        return x - width;
    default:
        return x - width * 0.5f;
    }
}
