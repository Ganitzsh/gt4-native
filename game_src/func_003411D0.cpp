/* compiler: ee-gcc2.96-nogcse */
typedef char s8;
typedef short s16;
typedef int s32;
typedef float f32;
#define M2C_FIELD(p,t,o) (*(t)((char *)(p)+(o)))
extern "C" void func_00105280(void *, int);          /* extern */
extern "C" int func_001052E0(void *, int, s32, s32); /* extern */
extern "C" void func_00105338(void *, void *, int); /* extern */
extern "C" void func_001056A0(void *);                   /* extern */
extern "C" void func_00105708(void *);                   /* extern */
extern "C" void func_00105728(void *);                   /* extern */
extern "C" s32 func_00105918(void *);                       /* extern */
extern "C" void func_00342370(int);                     /* extern */
extern "C" void func_00397098(void *, s32, f32, f32, f32);  /* extern */
extern "C" void func_00397130(void *, s32, f32, f32, f32);  /* extern */
extern "C" void func_00397DA0(void *, f32);             /* extern */
extern "C" void func_00456918(f32);                     /* extern */
extern "C" void func_004A1638(int);                     /* extern */
extern "C" void func_004A2808(s32, f32);                /* extern */
extern "C" void func_004A3078(int);                     /* extern */
extern "C" void func_004A3230(int);                     /* extern */
extern "C" void func_004A4550(int, int);            /* extern */
extern "C" void pglPushAttrib(int);                     /* extern */
extern "C" void func_004A5688();                            /* extern */
extern "C" void func_004A6788(f32, f32, f32, f32, f32, f32, f32, f32); /* extern */
extern "C" void func_004AA168(int);                     /* extern */
extern "C" void func_004AA1D8(f32, f32, f32, f32);      /* extern */
extern "C" void func_004AB040(int);                     /* extern */

static inline bool hasFlag(char *obj, int mask) { return (M2C_FIELD(obj, s32 *, 0x10) & mask) != 0; }

extern "C" void RacePS2Base__render_reflection(char *arg0, void *arg1, s32 arg2, s32 arg3, f32 fparg0) {
    struct StackFrame_003411D0 { s8 pad0[0x10]; s32 sp10, sp14, sp18, sp1C; s8 pad20[0x20]; s32 sp40; s32 sp44; s8 pad1[0x38]; } sp_;
    s32 *pb;
    f32 temp_f0;
    f32 temp_f20;
    f32 temp_f20_2;
    f32 temp_f21;
    f32 temp_f21_2;
    f32 temp_f21_3;
    f32 temp_f22;
    f32 var_f0;
    s32 temp_v0_9;
    s32 var_s0;
    char *temp_s0;
    char *temp_s1;
    char *temp_s3;
    char *temp_v0;
    char *temp_v0_2;
    char *temp_v0_3;
    char *temp_v0_4;
    char *temp_v0_5;
    char *temp_v0_6;
    char *temp_v0_7;
    char *temp_v0_8;
    char *temp_v1;

    temp_s3 = M2C_FIELD(M2C_FIELD(M2C_FIELD(arg0, char **, 0x6C), char **, 0x80), char **, 4);
    if ((M2C_FIELD(temp_s3, s32 *, 0x24) != 0) || (M2C_FIELD(temp_s3, s32 *, 0xC8) != 0)) {
        temp_v1 = M2C_FIELD(arg0, char **, 0x64) + 0xD0;
        temp_s0 = M2C_FIELD(temp_v1, char *(**)(char *, int, s32, s32, f32), 4)(arg0 + M2C_FIELD(temp_v1, s16 *, 0), 0, arg2, arg3, fparg0); /* $a2-$a3 and $f12 stay in place for the callee, so they are passed on. */
        if (M2C_FIELD(temp_s3, s32 *, 0x34) != 0) {
            func_00342370(0);
            temp_v0 = M2C_FIELD(temp_s0, char **, 0) + 0x30;
            temp_f22 = M2C_FIELD(temp_v0, f32 (**)(char *, f32), 4)(temp_s0 + M2C_FIELD(temp_v0, s16 *, 0), 0);
            temp_v0_2 = M2C_FIELD(temp_s0, char **, 0) + 0x28;
            temp_f21 = M2C_FIELD(temp_v0_2, f32 (**)(char *, f32), 4)(temp_s0 + M2C_FIELD(temp_v0_2, s16 *, 0), 0);
            temp_v0_3 = M2C_FIELD(temp_s0, char **, 0) + 0x20;
            func_00397130(temp_s3, arg3, M2C_FIELD(temp_v0_3, f32 (**)(char *, f32), 4)(temp_s0 + M2C_FIELD(temp_v0_3, s16 *, 0), 0), temp_f21, temp_f22);
        }
        pglPushAttrib(0x8C7);
        temp_s1 = M2C_FIELD(M2C_FIELD(M2C_FIELD(M2C_FIELD(arg0, char **, 0x6C), char **, 0x80), char **, 4), char **, 0x80);
        func_001052E0(((s8 *)&sp_), 0, M2C_FIELD(temp_s1, s32 *, 0xC24), M2C_FIELD(temp_s1, s32 *, 0xC28));
        pb = &sp_.sp40;
        func_00105338(pb, ((s8 *)&sp_), 0x31);
        func_001056A0(((s8 *)&sp_));
        func_00105708(pb);
        func_00456918(1.0f);
        func_004A3078(0);
        func_004A3230(3);
        func_00397DA0(temp_s1 + 0xC40, 1.0f);
        temp_v0_4 = M2C_FIELD(temp_s0, char **, 0) + 0x20;
        temp_f21_2 = M2C_FIELD(temp_v0_4, f32 (**)(char *, f32), 4)(temp_s0 + M2C_FIELD(temp_v0_4, s16 *, 0), 0);
        temp_v0_5 = M2C_FIELD(temp_s0, char **, 0) + 0x28;
        temp_f20 = M2C_FIELD(temp_v0_5, f32 (**)(char *, f32), 4)(temp_s0 + M2C_FIELD(temp_v0_5, s16 *, 0), 0);
        temp_v0_6 = M2C_FIELD(temp_s0, char **, 0) + 0x30;
        func_00397098(temp_s3, arg3, temp_f21_2, temp_f20, M2C_FIELD(temp_v0_6, f32 (**)(char *, f32), 4)(temp_s0 + M2C_FIELD(temp_v0_6, s16 *, 0), 0));
        func_00342370(0x80000000);
        if (hasFlag(temp_s1, 0x10000)) {
            temp_v0_7 = M2C_FIELD(arg0, char **, 0x64) + 0x388;
            M2C_FIELD(temp_v0_7, void (**)(char *, s32, f32), 4)(arg0 + M2C_FIELD(temp_v0_7, s16 *, 0), arg2, fparg0);
        }
        if (hasFlag(temp_s1, 0x20000)) {
            temp_v0_8 = M2C_FIELD(arg0, char **, 0x64) + 0x390;
            M2C_FIELD(temp_v0_8, void (**)(char *, f32), 4)(arg0 + M2C_FIELD(temp_v0_8, s16 *, 0), fparg0);
        }
        sp_.sp44 = 0;
        func_00105728(((s8 *)&sp_));
        if (M2C_FIELD(temp_s1, s32 *, 0xC2C) != 0) {
            float bias = 0.5f;
            func_004A4550(3, 1);
            func_004A4550(2, 1);
            func_004AB040(5);
            func_004AB040(4);
            func_004AB040(6);
            func_004AB040(8);
            var_s0 = 0;
            func_004AA168(0x80FFFFFF);
            func_001056A0(pb);
            if (M2C_FIELD(temp_s1, s32 *, 0xC2C) > 0) {
                do {
                    float w = (f32)sp_.sp18;
                    f32 h = (f32) func_00105918(((s8 *)&sp_));   /* fills sp_, read by the arguments after it */
                    func_004A6788(0.0f, 0.0f, w, h, (f32) sp_.sp10 + bias, (f32) sp_.sp14 + bias, (f32) (sp_.sp10 + sp_.sp18) + bias, (f32) (sp_.sp14 + sp_.sp1C) + bias);
                    if (var_s0 & 1) {
                        func_00105728(((s8 *)&sp_));
                        func_001056A0(pb);
                    } else {
                        func_00105728(pb);
                        func_001056A0(((s8 *)&sp_));
                    }
                    var_s0 += 1;
                } while (var_s0 < M2C_FIELD(temp_s1, s32 *, 0xC2C));
            }
        }
        func_004A5688();
        func_00456918(1.0f);
        func_004A4550(3, 1);
        func_004A4550(2, 1);
        func_004AB040(5);
        func_004AB040(4);
        func_004A1638(6);
        func_004AB040(8);
        func_004A2808(M2C_FIELD(temp_s1, s32 *, 0xC20), 0);
        temp_f0 = M2C_FIELD(temp_s1, f32 *, 0xC1C);
        func_004AA1D8(M2C_FIELD(temp_s1, f32 *, 0xC10) * temp_f0, M2C_FIELD(temp_s1, f32 *, 0xC14) * temp_f0, M2C_FIELD(temp_s1, f32 *, 0xC18) * temp_f0, 1.0f);
        var_f0 = (f32)M2C_FIELD(temp_s1, unsigned int *, 0xC2C);
        temp_f21_3 = (f32) M2C_FIELD(arg1, s32 *, 0x18);
        temp_f20_2 = var_f0 - 0x1.0000000000000p-1f;
        func_004A6788(0.0f, 0.0f, temp_f21_3, (f32) func_00105918(arg1), (f32) sp_.sp10, (f32) sp_.sp14, (f32) (sp_.sp10 + sp_.sp18) - temp_f20_2, (f32) (sp_.sp14 + sp_.sp1C) - temp_f20_2);
        func_00397DA0(temp_s1 + 0x60, 1.0f);
        func_001056A0(arg1);
        func_00105280(pb, 2);
        func_00105280(((s8 *)&sp_), 2);
    }
}
/* native correction of src/race/RaceBGMPS2_RacePS2Base/RacePS2Base__render_reflection.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_00397098; classes func_00397130 */
