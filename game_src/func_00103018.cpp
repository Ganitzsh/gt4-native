/* Native copy of 00103018 (matched), a start-up screen: its 16:9-mode x squeeze 0.75 is (4/3) / aspect (gt4_wide_squeeze). (docs/WIDESCREEN.md) */
extern "C" float gt4_wide_squeeze(void);
#include "types.h"
struct O { char p[0x14]; u16 u14; char q[2]; char *u18; };
extern "C" int func_00499728(void *);
extern "C" void func_0049CC48(void *);
extern "C" int func_0049FD50(s32, s32, s32, s32, s32, s32);
extern "C" void func_004A1638(s32);
extern "C" void func_004A2808(s32, float);
extern "C" void RaceEntryCar__update(s32);
extern "C" void func_004A53F8(void);
extern "C" void func_004A5400(void);
extern "C" void func_004A5A90(float, float, float, float, float, float);
extern "C" void func_004A7454(void);
extern "C" void func_004A7890(float, float, float);
extern "C" void func_004AA1D8(float, float, float, float);
extern "C" void func_004AB040(s32);
extern s32 D_006184F0;
extern "C" void func_00103018(void *self, float *f, O *o, s32 *a, s32 *b, s32 idx) {
    if (o) {
        if (!(idx < o->u14)) idx = 0;
        s32 off = idx * 4;
        void *s3 = o->u18 + idx * 0x28;
        RaceEntryCar__update(1);
        func_004A53F8();
        func_004A7454();
        func_004A5A90(-320.0f, 320.0f, -224.0f, 224.0f, -1.0f, 1.0f);
        if (D_006184F0) func_004A7890(gt4_wide_squeeze(), 1.0f, 1.0f);
        RaceEntryCar__update(0);
        func_004A53F8();
        func_004A7454();
        func_004AB040(0xF);
        func_004AB040(0x11);
        func_004AB040(5);
        func_004AB040(4);
        func_004A1638(2);
        func_004AB040(8);
        func_004A1638(6);
        func_004A2808(0x44, 1.0f);
        func_00499728(o);
        func_0049CC48(s3);
        func_004AA1D8(f[0], f[1], f[2], f[3]);
        func_0049FD50(4, a[idx], b[idx], 0, 0, 0);
        RaceEntryCar__update(1);
        func_004A5400();
        RaceEntryCar__update(0);
        func_004A5400();
    }
}
