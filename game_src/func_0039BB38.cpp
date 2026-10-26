/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native copy of RaceDisplay::setProjectionMatrix (0039BB38, matched) for any display aspect
 * (docs/WIDESCREEN.md): the race HUD's 16:9 orthographic edges (-106.67 .. 746.67, 853.33 units
 * centred on 320) become 320 -/+ 240 x aspect, so the HUD fills the host's shape. Other ranges
 * (4:3, or another layout) are kept. */
typedef float f32;
typedef int s32;
extern s32 D_006184F0;
extern "C" int gt4_display_aspect_milli(void);

struct func_0039BB38_mtx {
    f32 m0;
    s32 z4, z8, zC, z10;
    f32 m14;
    s32 z18, z1C, z20, z24;
    f32 m28;
    s32 z2C;
    f32 m30, m34;
    s32 z38;
    f32 m3C;
};

extern void RaceDisplay__setProjectionMatrix(char *arg0, f32 left, f32 right, f32 bottom, f32 top) {
    struct func_0039BB38_mtx *t = (struct func_0039BB38_mtx *)(arg0 + 0x70);
    const int aspect = gt4_display_aspect_milli();
    if (D_006184F0 && aspect >= 1500 && left < -100.0f && left > -110.0f && right > 740.0f && right < 750.0f) {
        const f32 half = 240.0f * (f32)aspect / 1000.0f;
        left = 320.0f - half;
        right = 320.0f + half;
    }
    t->m28 = -1.0f;
    t->z4 = t->z8 = t->zC = t->z10 = 0;
    t->z18 = t->z1C = t->z20 = t->z24 = 0;
    t->m0 = 2.0f / (right - left);
    t->z2C = 0;
    t->z38 = 0;
    t->m14 = 2.0f / (top - bottom);
    t->m30 = (left + right) / (left - right);
    t->m34 = (bottom + top) / (bottom - top);
    t->m3C = 1.0f;
}
