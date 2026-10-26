/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native copy of RaceDisplayUtil::wideOffset (003AFDD8, matched) for any display aspect
 * (docs/WIDESCREEN.md): an anchored HUD element (flags bit 2; bit 3 the right side) moves by 96 units
 * in the game's 16:9 mode, 0.9 of the 106.67 the wider projection adds on each side; for another
 * aspect the same 0.9 of what that projection adds (240 x aspect - 320). */
typedef float f32;
typedef int s32;
extern s32 D_006184F0;
int gt4_display_aspect_milli(void);

f32 RaceDisplayUtil__wideOffset(s32 flags) {
    f32 v;
    int aspect;
    if (!(flags & 4)) return 0.0f;
    if (D_006184F0 == 0) return 0.0f;
    aspect = gt4_display_aspect_milli();
    v = aspect >= 1500 ? 0.9f * (240.0f * (f32)aspect / 1000.0f - 320.0f) : 96.0f;
    return (flags & 8) ? v : -v;
}
