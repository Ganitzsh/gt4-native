/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native copy of 00216E78 from gt4-decomp's src/ui/mRenderContextPS2/func_00216E78.cpp (matched, 04da665dc), the
 * source the native game compiled until now: the garage's and showroom's car drawn n times, each pass jittered by
 * 004A2C58(n, i, 0.7) and blended into the last at 1 / (i + 1) (004A3998). 00217058 opens a kViewMenu view around each
 * call. Scene records (R6, CONTRACTS a.4): each pass between kMarkJitterPassBegin and kMarkJitterPassEnd (the pass
 * index is its order in the view, the count the number of pairs), sent between instances: before the pass's first
 * draw, after its blend. Nothing is sent while scene records are off. */
#include "gt4_gfx.h"
#include "spr.h"
struct Entry { short delta, index; void (*fn)(void *, int); };
struct Obj { char pad[0x48]; char *vtbl; };
extern "C" void func_00105670(void *);
extern "C" void func_001056A0(void *);
extern "C" void *func_00105AD8(void *, void *, int, int);
extern "C" float func_0021A7E8(Obj *);
extern "C" void func_004A2980(int);
extern "C" void func_004A29A8(int);
extern "C" void func_004A2C58(int, int, float);
extern "C" void func_004A3078(int);
extern "C" void func_004A3230(int);
extern "C" void func_004A3998(int, float);
extern "C" void func_004A4550(int, int);
extern "C" void func_004AB040(int);
extern "C" void func_00216E78(int unused, Obj *a, void *b, void *c, void *d, int n, int flag, Obj *e, int mode) {
    long long tmp;
    int i;
    if (n <= 0) n = 1;
    i = 0;
    func_004AB040(5);
    tmp = 0x80000000;
    func_00105670(d);
    {
        for (; i < n; i++) {
            int v = 0;
            gt4_scene_marker(spr_base(), 15);   /* kMarkJitterPassBegin */
            func_001056A0(b);
            if (flag == 0 || mode != 0) v = 1;
            func_00105AD8(b, c, 0, 0);
            func_004A3078(tmp);
            func_004A29A8(1);
            if (v != 0) func_004A2980(0xFF000000);
            if (mode != 0) func_004A3230(1); else func_004A3230(3);
            func_004A2980(-1);
            func_004A4550(1, 0);
            func_004A2C58(n, i, 0.7f);
            Entry *en = (Entry *)(a->vtbl + 0x28);
            en->fn((char *)a + en->delta, flag);
            if (e != 0) {
                Entry *en2 = (Entry *)(e->vtbl + 0x28);
                en2->fn((char *)e + en2->delta, flag);
            }
            func_001056A0(b);
            func_00105670(d);
            float f = mode != 0 ? func_0021A7E8(a) : 1.0f / (float)(i + 1);
            func_004A3998(6, f);
            gt4_scene_marker(spr_base(), 16);   /* kMarkJitterPassEnd */
        }
    }
}
