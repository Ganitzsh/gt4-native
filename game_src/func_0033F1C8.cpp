/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native copy of 0033F1C8 from gt4-decomp's src/race/RaceBGMPS2_RacePS2Base/func_0033F1C8.cpp (matched), the source the
 * native game compiled until now, with the glare (003BADF8) between kMarkGlareBegin and kMarkGlareEnd (A5b; CONTRACTS.md
 * a.4). Nothing is sent while scene records are off. */
#include "gt4_gfx.h"
#include "spr.h"
/* Fills a six-field parameter block at self+0x3604 from a settings object and the size of a frame record, then
 * passes it to func_003BADF8. The settings object (local s) is found through four pointer hops from self+0x6C
 * (+0x80, +4, +0x80) and is used only when bit 0 of its flags at +0x1750 is set. The frame record has its width at
 * +8 and its height from func_00105818. scale is the frame diagonal (sqrt of width squared plus height squared,
 * using the sqrt.s instruction as inline asm) divided by the hex float 0x1.869c18p+9, about 781.2, which is the
 * diagonal of a 640x448 screen (inferred). The block receives the four floats of the settings at +0x1754..+0x1760
 * (the last one times scale), the word at +0x1764, half the width and a quarter of the height. Meaning of the
 * fields is unknown. */
typedef int s32;

static inline float sqrt_s(float x) { float r; __asm__("sqrt.s %0, %1" : "=f"(r) : "f"(x)); return r; }

struct Out { float f0, f4, f8, fC; s32 i10; s32 i14; s32 i18; };
struct Src { char pad[0x1750]; s32 flags; float f1754, f1758, f175C, f1760; s32 i1764; };
struct Dim { char pad[8]; s32 w; };

extern "C" s32 func_00105818(void *);
extern "C" void func_003BADF8(void *, s32, void *);

extern "C" void func_0033F1C8(char *self, Dim *frame, s32 param) {
    Src *s = *(Src **)(*(char **)(*(char **)(*(char **)(self + 0x6C) + 0x80) + 4) + 0x80);
    Out *out = (Out *)(self + 0x3604);
    if (s->flags & 1) {
        float width = (float)frame->w;
        float height = (float)func_00105818(frame);
        s32 halfWidth = frame->w / 2;
        float scale = sqrt_s(width * width + height * height) / 0x1.869c18p+9f;
        s32 quarterHeight = func_00105818(frame) / 4;
        out->i14 = halfWidth;
        out->i18 = quarterHeight;
        out->f0 = s->f1754;
        out->f4 = s->f1758;
        out->f8 = s->f175C;
        out->fC = s->f1760 * scale;
        out->i10 = s->i1764;
        gt4_scene_marker(spr_base(), 9);    /* kMarkGlareBegin */
        func_003BADF8(out, param, frame);
        gt4_scene_marker(spr_base(), 10);   /* kMarkGlareEnd */
    }
}
