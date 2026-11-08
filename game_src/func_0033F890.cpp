/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native copy of 0033F890 (RacePS2Base::render_post_effect) from gt4-decomp's
 * src/race/RaceBGMPS2_RacePS2Base/RacePS2Base__render_post_effect.cpp, the source the native game compiled until now, with
 * the post effect's markers (docs/modern/CONTRACTS.md a.4, a.5 5): kMarkPostBegin at its start, kMarkPostEnd at its end.
 * Nothing is sent while scene records are off. */
#include "gt4_gfx.h"
#include "spr.h"
typedef int s32;
typedef short s16;

static inline float minf(float a, float b) { float r; __asm__("min.s %0, %1, %2" : "=f"(r) : "f"(a), "f"(b)); return r; }

struct VEntry {
    s16 delta;
    s16 index;
    void *fn;
};

struct E { char pad[0x3C]; };
struct T { E e[2]; s32 cur; };
extern T D_0061850C;

extern "C" void func_00105670(void *);
extern "C" void func_001056A0(void *);
extern "C" void func_003429E0(void *, void *);
extern "C" void func_004A2980(s32);
extern "C" void func_004A29A8(s32);
extern "C" void func_004A3998(s32, float);

typedef char *(*GetFn)(char *, s32);
typedef s32 (*TestFn)(char *);
typedef float (*FFn)(char *);

extern "C" void RacePS2Base__render_post_effect(char *arg0, void *arg1) {
    gt4_scene_marker(spr_base(), 1);   /* kMarkPostBegin */
    if (*(unsigned char *)(arg0 + 0xCF5A) == 0) {
        *(float *)(arg0 + 0xCF40) = *(float *)(arg0 + 0xCF44);
        func_003429E0(arg0, arg1);
    } else {
        VEntry *e1 = (VEntry *)(*(char **)(arg0 + 0x64) + 0xD0);
        char *o = ((GetFn)e1->fn)(arg0 + e1->delta, 0);
        VEntry *e2 = (VEntry *)(*(char **)o + 0x170);
        if (((TestFn)e2->fn)(o + e2->delta) != 0) {
            VEntry *e3 = (VEntry *)(*(char **)(arg0 + 0x64) + 0xD0);
            char *o2 = ((GetFn)e3->fn)(arg0 + e3->delta, 0);
            VEntry *e4 = (VEntry *)(*(char **)o2 + 0x178);
            *(float *)(arg0 + 0xCF40) = ((FFn)e4->fn)(o2 + e4->delta);
            func_003429E0(arg0, arg1);
        }
    }
    VEntry *e5 = (VEntry *)(*(char **)(arg0 + 0x64) + 0xD0);
    char *p = ((GetFn)e5->fn)(arg0 + e5->delta, 0);
    VEntry *e6 = (VEntry *)(*(char **)p + 0x180);
    if (((TestFn)e6->fn)(p + e6->delta) != 0) {
        func_00105670(arg1);
        func_001056A0(&D_0061850C.e[1 - D_0061850C.cur]);
        func_004A2980(0xFFFFFF);
        func_004A29A8(0);
        VEntry *e7 = (VEntry *)(*(char **)p + 0x188);
        float f = ((FFn)e7->fn)(p + e7->delta);
        func_004A3998(6, minf(f, 0x1.999998p-1f));
        func_004A2980(-1);
        func_004A29A8(1);
    }
    gt4_scene_marker(spr_base(), 2);   /* kMarkPostEnd */
}
