/* compiler: ee-gcc2.96 */
/* Native copy of the race HUD's integer alignment (003AFD78, matched) for any display aspect
 * (docs/WIDESCREEN.md): an anchored element (flags bit 2; bit 3 the right side) moves by
 * RaceDisplayUtil::wideOffset's amount (game_src/func_003AFDD8.c), rounded, instead of the 16:9
 * mode's 96 units; the low two bits then align it (0 left, 1 right, else centred). */
#include "types.h"

f32 RaceDisplayUtil__wideOffset(s32 flags);

s32 func_003AFD78(s32 x, s32 w, s32 fl) {
    const f32 shift = RaceDisplayUtil__wideOffset(fl);
    x += (s32)(shift < 0.0f ? shift - 0.5f : shift + 0.5f);
    switch (fl & 3) {
    case 0: return x;
    case 1: return x - w;
    default: return x - (w >> 1);
    }
}
