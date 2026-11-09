/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native copy of 0033FD88 (RacePS2Base::render_world_split: one half of 2 Player Battle's split screen, twice a
 * frame from RaceSplitBattleBase::render, 0032FCC8) from gt4-decomp's
 * src/race/RaceBGMPS2_RacePS2Base/RacePS2Base__render_world_split.cpp, with A12's scene records (PLAN A12): the
 * half's main view (kViewMain; the runtime adds kViewSplit, the half and its scissor) from its camera load to
 * before the mirror, the reflection bracket around method 0x378, the flare markers around 003B8848 and 003B88C8,
 * step 1a's glow lights before 003B88C8 and A7 step 2's car lamps after the glow pass (method 0x3F0), as
 * 0033EAB8's and 0033F2E8's copies do for a single view. Nothing is sent while scene records are off. */
typedef char s8;
typedef short s16;
typedef int s32;
typedef float f32;
#define M2C_FIELD(p,t,o) (*(t)((char *)(p)+(o)))
struct Glob { char pad[0x18]; float f18; int f1c; char pad20[0x114]; int f134; }; extern Glob D_006184D0;
struct View { char data[0x3c]; }; struct Views { View v[2]; int cur; }; extern Views D_0061850C;
struct Flag { int value; }; extern Flag D_00621D24;
static inline void setFlag(int f, Flag *p) { p->value = f; }
static inline bool hasFlag(int mask, int flags) { return flags & mask; }
#include "gt4_gfx.h"
#include "spr.h"
extern "C" int func_00105250(void *);                   /* extern */
extern "C" void func_00105280(void *, int);          /* extern */
extern "C" int func_001052E0(void *, int, int, int); /* extern */
extern "C" void func_001053F8(void *, s32);              /* extern */
extern "C" void func_00105440(void *, s32);              /* extern */
extern "C" void func_00105450(void *, s32);              /* extern */
extern "C" void func_001054A0(void *, int);          /* extern */
extern "C" int func_00105528(void *);                   /* extern */
extern "C" void func_001056A0(void *);                   /* extern */
extern "C" void func_00105708(void *);                   /* extern */
extern "C" f32 func_001058E8(void *);                          /* extern */
extern "C" void func_00105C48(void *, void *, s32, int, f32, f32, f32, f32); /* extern */
extern "C" void func_00342470(void *);                   /* extern */
extern "C" void func_00342580(void *, f32, f32);     /* extern */
extern "C" void func_00396B80(void *, s32, int, f32, f32); /* extern */
extern "C" void func_00396D40(void *);                      /* extern */
extern "C" void CourseData__render_course(void *, s32, void *, s32);    /* extern */
extern "C" void func_00397CE0(void *);                      /* extern */
extern "C" void func_00397D48(void *);                      /* extern */
extern "C" void func_00397DF8(void *);                      /* extern */
extern "C" void func_003985A0(void *, void *);              /* extern */
extern "C" void func_00398650(void *, void *);              /* extern */
extern "C" void func_003B7B68(void *, int);             /* extern */
extern "C" void func_003B80E8(void *, void *);           /* extern */
extern "C" void func_003B8848(void *);                      /* extern */
extern "C" void func_003B88C8(void *);                      /* extern */
extern "C" s32 Pitmen__isNotRenderOthersPeriod(void *, s16);                     /* extern */
extern "C" void func_003E50E0(void *);                      /* extern */
extern "C" void func_003E5338(void *);                      /* extern */
extern "C" void func_003E7CC8(void *, void *, int, s16); /* extern */
extern "C" void func_0044E9A0(void *, void *, void *, s32, int, int, f32); /* extern */
extern "C" int ModelSet2__texLOD(int);                     /* extern */
extern "C" void func_00456828(f32);                         /* extern */
extern "C" int ModelSet2__texLevel(int);                     /* extern */
extern "C" void func_00456918(f32);                     /* extern */
extern "C" void func_004A1638(int);                     /* extern */
extern "C" void func_004A3078(int);                     /* extern */
extern "C" void func_004A3230(int);                     /* extern */
extern "C" void RaceEntryCar__update(int);                     /* extern */
extern "C" void func_004A7454();                            /* extern */
extern "C" void func_004A7890(f32, f32, f32);           /* extern */
extern "C" void func_004AB040(int);                     /* extern */

extern "C" void RacePS2Base__render_world_split(char *arg0, char *arg1, s32 arg2, f32 fparg0) {
    struct StackFrame_0033FD88 { s32 sp0; s32 sp4; s8 pad1[0x38]; s32 sp40; s32 sp44; s8 pad2[0x38]; s32 sp80; s8 pad3[0x3C]; } sp_;
    f32 temp_f13;
    f32 temp_f20;
    f32 var_f1;
    s32 temp_a1;
    char *temp_a2;
    char *temp_s0;
    char *temp_s3;
    char *temp_s4;
    char *temp_s5;
    char *temp_s6;
    char *temp_v0;
    char *temp_v0_2;
    char *temp_v0_3;
    char *temp_v0_4;
    char *temp_v0_5;
    char *temp_v0_6;
    char *temp_v0_7;
    char *temp_v1;
    char *temp_v1_2;

    temp_v1 = M2C_FIELD(arg0, char **, 0x64) + 0xD0;
    temp_a2 = M2C_FIELD(M2C_FIELD(arg0, char **, 0x6C), char **, 0x80);
    temp_s4 = M2C_FIELD(temp_a2, char **, 4);
    temp_s3 = M2C_FIELD(temp_s4, char **, 0x80);
    temp_s5 = M2C_FIELD(temp_v1, char *(**)(char *, int, f32), 4)(arg0 + M2C_FIELD(temp_v1, s16 *, 0), 0, fparg0);
    func_00105250(((s8 *)&sp_));
    sp_.sp4 = 0;
    func_00105440(((s8 *)&sp_), M2C_FIELD(arg1, s32 *, 0x18));
    func_00105450(((s8 *)&sp_), M2C_FIELD(arg1, s32 *, 0x1C) * 2);
    func_001054A0(((s8 *)&sp_), (D_006184D0.f1c != 0) ? 9 : 0);
    func_00105528(((s8 *)&sp_));
    func_00105250(&sp_.sp40);
    sp_.sp44 = 0x31;
    func_00105440(&sp_.sp40, M2C_FIELD(arg1, s32 *, 0x18));
    func_00105450(&sp_.sp40, M2C_FIELD(arg1, s32 *, 0x1C) * 2);
    func_001054A0(&sp_.sp40, (D_006184D0.f1c != 0) ? 9 : 0);
    func_001053F8(&sp_.sp40, D_006184D0.f134);
    func_00456828(M2C_FIELD(temp_s3, f32 *, 0x50));
    func_001052E0(&sp_.sp80, 2, 0x100, 0x100);
    if ((arg2 == 0) || !hasFlag(0x100000, M2C_FIELD(temp_s3, s32 *, 0x10))) {
        func_0044E9A0(&sp_.sp80, temp_s4, temp_s5, sp_.sp0, 0x80, 0x100, 0);
    } else {
        func_001056A0(&sp_.sp80);
        func_004A3078(0x80000000);
        func_004A3230(1);
    }
    func_001056A0(((s8 *)&sp_));
    func_00105708(&sp_.sp40);
    func_004A3078(0);
    func_004A3230(3);
    RaceEntryCar__update(1);
    func_004A7454();
    var_f1 = M2C_FIELD(temp_s3, f32 *, 0x20);
    if (arg2 != 0) {
        var_f1 *= 0x1.0000000000000p-1f;
    }
    temp_v0 = M2C_FIELD(temp_s5, char **, 0) + 0x10;
    temp_s6 = arg0 + 0xDA0;
    M2C_FIELD(temp_s5, f32 *, 0xA08) = var_f1;
    M2C_FIELD(temp_v0, void (**)(char *, f32), 4)(temp_s5 + M2C_FIELD(temp_v0, s16 *, 0), 0);
    temp_f20 = func_001058E8(arg1);
    func_004A7890((temp_f20 / func_001058E8(&D_0061850C.v[D_0061850C.cur])) * fparg0, fparg0, 1.0f);
    func_00456918(1.0f);
    RaceEntryCar__update(0);
    func_004A7454();
    temp_v0_2 = M2C_FIELD(temp_s5, char **, 0) + 0x18;
    M2C_FIELD(temp_v0_2, void (**)(char *, f32), 4)(temp_s5 + M2C_FIELD(temp_v0_2, s16 *, 0), 0);
    gt4_scene_view_begin(spr_base(), 1, 4, 0);   /* kViewMain, kViewCameraKnown: the half's camera is set 0 now */
    func_00397CE0(temp_s3);
    func_00396B80(temp_s4, M2C_FIELD(temp_s4, s32 *, 0x54), -1, 1.0f, 1000.0f);
    func_00398650(temp_s3, temp_s6);
    func_00397DF8(temp_s3);
    func_00396D40(temp_s4);
    func_004A3230(2);
    func_00342470(((s8 *)&sp_));
    ModelSet2__texLOD(1);
    ModelSet2__texLevel(arg2 ? 2 : -1);
    if (arg2 != 0) {
        CourseData__render_course(temp_s4, M2C_FIELD(temp_s4, s32 *, 0x10), temp_s5, M2C_FIELD(M2C_FIELD(temp_s4, char **, 0x80), s32 *, 0xF34));
    } else {
        CourseData__render_course(temp_s4, M2C_FIELD(temp_s4, s32 *, 8), temp_s5, M2C_FIELD(M2C_FIELD(temp_s4, char **, 0x80), s32 *, 0xF30));
    }
    ModelSet2__texLOD(0);
    if (hasFlag(0x01000000, M2C_FIELD(temp_s3, s32 *, 0x10))) {
        temp_v0_3 = M2C_FIELD(arg0, char **, 0x64) + 0x378;
        gt4_scene_reflection(spr_base(), 1);   /* render_reflection (method 0x378): kInstReflection, A6 */
        M2C_FIELD(temp_v0_3, void (**)(char *, void *, void *, int, f32), 4)(arg0 + M2C_FIELD(temp_v0_3, s16 *, 0), ((s8 *)&sp_), &sp_.sp80, 1, 0);
        gt4_scene_reflection(spr_base(), 0);
    }
    func_00397D48(temp_s3);
    func_00342580(((s8 *)&sp_), M2C_FIELD(temp_s3, f32 *, 0x3C), 1.0f);
    func_004A1638(5);
    temp_v0_4 = M2C_FIELD(arg0, char **, 0x64) + 0x380;
    M2C_FIELD(temp_v0_4, void (**)(char *, void *, int, int, int, f32), 4)(arg0 + M2C_FIELD(temp_v0_4, s16 *, 0), &sp_.sp80, (arg2 == 0) ? 0 : -1, 0, -1, 0);
    if (M2C_FIELD(M2C_FIELD(arg0, char **, 0x6C), s32 *, 0xE0) == 0) {
        if (hasFlag(0x800, M2C_FIELD(temp_s3, s32 *, 0x10))) {
            setFlag(1, &D_00621D24);
            func_003E50E0(temp_s3 + 0xAD0);
        } else {
            setFlag(0, &D_00621D24);
        }
        func_003E5338(arg0 + 0x35E8);
    }
    gt4_scene_view_end(spr_base());   /* the half's instances are drawn; its mirror is a view of its own */
    if (M2C_FIELD(arg0, s32 *, 0xCC8) == 0) {
        temp_v1_2 = M2C_FIELD(temp_s5, char **, 0) + 0x1A0;
        if (M2C_FIELD(temp_v1_2, s32 (**)(char *), 4)(temp_s5 + M2C_FIELD(temp_v1_2, s16 *, 0)) != 0) {
            temp_v0_5 = M2C_FIELD(arg0, char **, 0x64) + 0x3C0;
            M2C_FIELD(temp_v0_5, void (**)(char *, void *, int), 4)(arg0 + M2C_FIELD(temp_v0_5, s16 *, 0), ((s8 *)&sp_), 1);
        }
    }
    func_003B7B68(temp_s6, 1);
    func_003B80E8(temp_s6, ((s8 *)&sp_));
    func_003985A0(temp_s3, temp_s6);
    temp_a1 = M2C_FIELD(temp_s3, s32 *, 0x10);
    if (!hasFlag(0x400000, temp_a1)) {
        int enabled = 0; if (hasFlag(0x8000, temp_a1)) enabled = 1;
        temp_v0_6 = M2C_FIELD(arg0, char **, 0x64) + 0x3F0;
        M2C_FIELD(temp_v0_6, void (**)(char *, bool, f32), 4)(arg0 + M2C_FIELD(temp_v0_6, s16 *, 0), enabled, 0);
    }
    if (hasFlag(0x200000, M2C_FIELD(temp_s3, s32 *, 0x10))) { if (!hasFlag(0x400000, M2C_FIELD(temp_s3, s32 *, 0x10))) {
        temp_v0_7 = M2C_FIELD(arg0, char **, 0x64) + 0x3F8;
        M2C_FIELD(temp_v0_7, void (**)(char *), 4)(arg0 + M2C_FIELD(temp_v0_7, s16 *, 0));
    }
    }
    /* A7 step 2: every car's lamps as Lights of the half's view, after the glow pass (or at its place) */
    gt4_scene_car_lamps(spr_base(), 0, arg0);
    temp_s0 = arg0 + 0x31D0;
    gt4_scene_marker(spr_base(), 7);   /* kMarkFlareBegin: the flare's read (A5b) */
    func_003B8848(temp_s6);
    gt4_scene_marker(spr_base(), 8);   /* kMarkFlareEnd */
    M2C_FIELD(temp_s0, f32 *, 0x40) = (f32) (0x1.0000000000000p+0f / M2C_FIELD(temp_s3, f32 *, 0x38));
    if (Pitmen__isNotRenderOthersPeriod(arg0 + 0x3628, M2C_FIELD(((M2C_FIELD(temp_s5, s32 *, 0x9BC) * 0x19C) + temp_s5), s16 *, 0x1C)) != 0) {
        func_003E7CC8(temp_s0, &sp_.sp40, 1, M2C_FIELD(((M2C_FIELD(temp_s5, s32 *, 0x9BC) * 0x19C) + temp_s5), s16 *, 0x1C));
    } else {
        func_003E7CC8(temp_s0, &sp_.sp40, 1, -1);
    }
    gt4_scene_marker(spr_base(), 7);   /* kMarkFlareBegin */
    /* A7: the glow pass's lists as it draws them, as Lights of the half's view */
    gt4_scene_glow_lights(spr_base(), 0, temp_s6);
    func_003B88C8(temp_s6);
    gt4_scene_marker(spr_base(), 8);   /* kMarkFlareEnd */
    func_004AB040(5);
    temp_f13 = M2C_FIELD(temp_s3, f32 *, 0x38);
    func_00105C48(arg1, ((s8 *)&sp_), D_006184D0.f1c, 0, D_006184D0.f18, temp_f13, temp_f13, temp_f13);
    func_004AB040(0xF);
    func_004AB040(5);
    func_00105280(&sp_.sp80, 2);
    func_00105280(&sp_.sp40, 2);
    func_00105280(((s8 *)&sp_), 2);
}
