/* compiler: ee-gcc2.96-no-strict-aliasing */
// Native correction of the draft build/swarm/a6/003b5e80_best.cpp (no matched source): it declares func_0057FB50 (fptodp)
// as returning double, but the soft-float helper returns the double's bits as a 64-bit integer
// (edx:eax natively, as gt4-native implements it), so the double read from st(0) was garbage.
// Declared as returning the bits here and converted back to double through a union (bit-exact).
#include "types.h"
#include "m2c_macros.h"
extern long D_006A2580[];
struct Packet { s32 sp0; char pad4[0x4]; f32 sp8; char padc[0x4]; s64 sp10; s64 sp18; s64 sp20; s64 sp28; s64 sp30; s64 sp38; s32 sp40; s32 sp44; s32 sp48; f32 sp4C; f32 sp50; f32 sp54; f32 sp58; f32 sp5C; f32 sp60; f32 sp64; f32 sp68; f32 sp6C; f32 sp70; f32 sp74; f32 sp78; f32 sp7C; f32 sp80; f32 sp84; f32 sp88; f32 sp8C; int sp90; char pad94[0x20c]; f32 sp2A0; f32 sp2A4; f32 sp2A8; f32 sp2AC; s32 sp2B0; s32 sp2B4; s32 sp2B8; f32 sp2BC; float sp2C0; float sp2C4; f32 sp2C8; char pad2cc[0x4]; };
extern "C" f32 func_00344720(char *);                             /* extern */
extern "C" f32 func_00344748(char *);                             /* extern */
extern "C" f32 func_00344770(char *);                             /* extern */
extern "C" f32 func_00344798(char *, float);                    /* extern */
extern "C" f32 func_003447F8(char *, float);                    /* extern */
extern "C" f32 func_00344828(char *, float);                    /* extern */
extern "C" void func_00344FC0(char *, void *, s32, float);    /* extern */
extern "C" f32 func_0034FA90(char *, u16 *);                      /* extern */
extern "C" f32 func_0034FBC0(char *, f32 *);                      /* extern */
extern "C" f32 func_0034FED0(char *, f32 *);                      /* extern */
extern "C" f32 func_00350500(char *, int *, s32 *, int *); /* extern */
extern "C" f32 func_00354EB0(char *);                             /* extern */
extern "C" f32 func_00355950(char *);                             /* extern */
extern "C" void func_0035FFB0(char *, s32, void *, void *, f32, f32); /* extern */
extern "C" void func_00360028(char *, s32, void *, void *, f32, f32); /* extern */
extern "C" void func_003600E8(char *, s32, void *, void *, f32, f32); /* extern */
extern "C" void func_00360158(char *, s32, void *, void *, f32, f32); /* extern */
extern "C" void func_003601A8(char *, s32, void *, void *, f32, f32); /* extern */
extern "C" void func_00360200(char *, s32, void *, void *, f32, f32); /* extern */
extern "C" void func_003602A0(char *, s32, void *, void *, f32, f32); /* extern */
extern "C" void func_003602C8(char *, s32, void *, void *, f32, f32); /* extern */
extern "C" void func_003602F0(char *, s32, void *, void *, f32, f32); /* extern */
extern "C" void func_003602F8(char *, s32, void *, void *, f32, f32); /* extern */
extern "C" void func_00360320(char *, s32, void *, void *, f32, f32); /* extern */
extern "C" void func_00372080(char *, s32);                 /* extern */
extern "C" void func_00391C28(void *, f32);                  /* extern */
extern "C" void func_003924D0(char *);                         /* extern */
extern "C" void func_00392640(char *, char *);                 /* extern */
extern "C" void func_00392FC8(char *, int);                /* extern */
extern "C" s32 func_003B6B78(char *, int);                 /* extern */
extern "C" f32 func_003F3A68(s32, int);           /* extern */
extern "C" float func_003FA7B8(char *, s32);                    /* extern */
extern "C" s32 func_003FA910(char *);                             /* extern */
extern "C" void func_004A5348(int);                     /* extern */
extern "C" void func_004A7454();                            /* extern */
extern "C" void func_004A7AA8(void *, void *);                /* extern */
extern "C" s32 func_00579358(s32);                             /* extern */
extern "C" s32 func_0057F6F8(double, double);                    /* extern */
extern "C" long func_0057FB50(f32); /* the double's bits */
static inline double bits_to_double(long l) { union { long l; double d; } u; u.l = l; return u.d; }

static inline int remap(int n) { return n == 4 ? 1 : n; }
static inline bool multiple(int n) { return n >= 2; }
extern "C" void func_003B5E80(char *arg0, char *arg1, s32 arg2) {
    Packet packet;
    union Vec { s64 l[2]; float f[4]; } vec;
    u16 sp2E0;
    f32 sp2E4;
    int sp2E8;
    s32 sp2EC;
    int sp2F0;
    s32 sp2F4;
    s32 sp2F8;
    char *sp2FC;
    char *sp300;
    s64 *sp304;
    char *sp308;
    char *sp30C;
    f32 temp_f0;
    f32 temp_f0_2;
    f32 temp_f0_3;
    f32 temp_f20;
    f32 temp_f20_10;
    f32 temp_f20_11;
    f32 temp_f20_2;
    f32 temp_f20_3;
    f32 temp_f20_4;
    f32 temp_f20_5;
    f32 temp_f20_6;
    f32 temp_f20_7;
    f32 temp_f20_8;
    f32 temp_f20_9;
    f32 temp_f21;
    f32 temp_f21_2;
    f32 temp_f22;
    f32 temp_f22_2;
    f32 temp_f23;
    f32 temp_f23_2;
    f32 temp_f23_3;
    f32 temp_f24;
    f32 temp_f24_2;
    f32 temp_f24_3;
    f32 temp_f25;
    f32 temp_f25_2;
    f32 temp_f25_3;
    f32 temp_f26;
    f32 temp_f26_2;
    f32 temp_f27;
    f32 temp_f27_2;
    f32 temp_f28;
    f32 temp_f29;
    f32 temp_f29_2;
    f32 temp_f30;
    f32 temp_f30_2;
    f32 temp_f31;
    f32 var_f0;
    f32 var_f0_2;
    f32 var_f20;
    f32 var_f21;
    f32 var_f22;
    s32 temp_a0;
    s32 temp_a0_2;
    s32 temp_a1_3;
    s32 temp_a1_4;
    s32 temp_s0;
    s32 temp_v0_11;
    char *temp_v0_2;
    s32 temp_v0_9;
    s32 temp_v1;
    s32 var_a1;
    s32 var_s0;
    s32 var_s3;
    s32 var_s7;
    char *temp_a1;
    char *temp_a1_2;
    char *temp_a4;
    char *temp_s0_2;
    char *temp_s1;
    char *temp_s2;
    char *temp_s3;
    char *temp_s4;
    char *temp_s4_2;
    char *temp_s5;
    char *temp_s6;
    char *temp_v0;
    char *temp_v0_10;
    char *temp_v0_12;
    char *temp_v0_13;
    char *temp_v0_3;
    char *temp_v0_4;
    char *temp_v0_5;
    char *temp_v0_6;
    char *temp_v0_7;
    char *temp_v0_8;
    char *temp_v1_2;
    char *temp_v1_3;
    char *temp_v1_4;
    char *temp_v1_5;
    char *temp_v1_6;
    char *var_fp;

    temp_a1 = M2C_FIELD(arg1, char **, 0);
    packet.sp8 = 0x1.0000000000000p+0f;
    sp2F4 = arg2;
    temp_a1_2 = temp_a1 + 0x100;
    sp2FC = arg0 + 0x2900;
    sp2F8 = M2C_FIELD(arg0, s32 *, 0x198) == 1;
    /* The EE forwards arg2 to this first slot-0x100 virtual in $a2 ($a1 then holds the vtable entry) (passthrough_lint). */
    if (M2C_FIELD(temp_a1_2, s32 (**)(char *, char *, s32), 4)(arg1 + M2C_FIELD(temp_a1, s16 *, 0x100), temp_a1_2, arg2) == 0) {
        func_00392FC8(sp2FC, 0);
        func_003924D0(sp2FC);
    }
    temp_v1 = M2C_FIELD(arg1, s32 *, 0x9B8);
    if (multiple(temp_v1)) {
        temp_s0 = M2C_FIELD(arg0, s32 *, 0x14);
        if (temp_s0 < temp_v1) {
            func_004A5348(0);
            func_004A7454();
            var_a1 = temp_s0;
            if (var_a1 != M2C_FIELD(((var_a1 * 0x19C) + arg1), s16 *, 0x1C)) {
                if (var_a1 != M2C_FIELD(arg1, s16 *, 0x1C)) {
                    var_a1 = ((var_a1 ^ M2C_FIELD(arg1, s16 *, 0x1B8)) == 0) ? 1 : var_a1;
                } else {
                    var_a1 = 0;
                }
            }
            func_00372080(arg1, var_a1);
            temp_v0 = M2C_FIELD(arg1, char **, 0) + 0x18;
            M2C_FIELD(temp_v0, void (**)(char *, float), 4)(arg1 + M2C_FIELD(temp_v0, s16 *, 0), 0x0.0p+0f);
        }
    }
    temp_v0_2 = M2C_FIELD(arg0, char **, 0x18);
    sp300 = temp_v0_2;
    temp_s4 = temp_v0_2 + 0x104;
    temp_f29 = func_00344798(temp_v0_2, 0);
    temp_f27 = func_003447F8(sp300, 0);
    var_f22 = 0x1.0000000000000p+0f;
    temp_f26 = func_00344828(sp300, 0);
    Vec *pv = &vec;
    pv->f[0] = temp_f29;
    vec.f[1] = temp_f27;
    vec.f[2] = temp_f26;
    vec.f[3] = 0x1.0000000000000p+0f;
    func_004A7AA8(pv, pv);
    temp_v1_2 = M2C_FIELD(arg1, char **, 0) + 0x168;
    packet.sp0 = M2C_FIELD(temp_v1_2, s32 (**)(char *, char *), 4)(arg1 + M2C_FIELD(temp_v1_2, s16 *, 0), arg0);
    temp_v1_3 = M2C_FIELD(arg1, char **, 0) + 0x100;
    if (M2C_FIELD(temp_v1_3, s32 (**)(char *), 4)(arg1 + M2C_FIELD(temp_v1_3, s16 *, 0)) != 0) {
        temp_v0_3 = M2C_FIELD(arg1, char **, 0) + 0x20;
        temp_f30 = M2C_FIELD(temp_v0_3, f32 (**)(char *, float), 4)(arg1 + M2C_FIELD(temp_v0_3, s16 *, 0), 0);
        temp_f25 = temp_f29 - temp_f30;
        temp_v0_4 = M2C_FIELD(arg1, char **, 0) + 0x28;
        temp_f20 = M2C_FIELD(temp_v0_4, f32 (**)(char *, float), 4)(arg1 + M2C_FIELD(temp_v0_4, s16 *, 0), 0);
        temp_f24 = temp_f27 - temp_f20;
        temp_v0_5 = M2C_FIELD(arg1, char **, 0) + 0x30;
        temp_f0 = M2C_FIELD(temp_v0_5, f32 (**)(char *, float), 4)(arg1 + M2C_FIELD(temp_v0_5, s16 *, 0), 0);
        temp_f23 = temp_f26 - temp_f0;
        var_f0 = (temp_f25 * temp_f25) + (temp_f24 * temp_f24) + (temp_f23 * temp_f23);
        __asm__("sqrt.s %0,%1" : "=f"(var_f0) : "f"(var_f0));
        if (var_f0 == 0x0.0p+0f) {
            var_f0 = 0x1.0000000000000p+0f;
        }
        temp_v0_6 = M2C_FIELD(arg1, char **, 0) + 0xE0;
        temp_f23_2 = temp_f23 / var_f0;
        temp_f25_2 = temp_f25 / var_f0;
        temp_f24_2 = temp_f24 / var_f0;
        temp_f22 = temp_f25_2 * (temp_f30 - M2C_FIELD(temp_v0_6, f32 (**)(char *, int), 4)(arg1 + M2C_FIELD(temp_v0_6, s16 *, 0), 1));
        temp_v0_7 = M2C_FIELD(arg1, char **, 0) + 0xE8;
        temp_f20_2 = temp_f24_2 * (temp_f20 - M2C_FIELD(temp_v0_7, f32 (**)(char *, int), 4)(arg1 + M2C_FIELD(temp_v0_7, s16 *, 0), 1));
        temp_v0_8 = M2C_FIELD(arg1, char **, 0) + 0xF0;
        temp_f22_2 = temp_f22 + temp_f20_2 + (temp_f23_2 * (temp_f0 - M2C_FIELD(temp_v0_8, f32 (**)(char *, int), 4)(arg1 + M2C_FIELD(temp_v0_8, s16 *, 0), 1)));
        temp_f20_3 = temp_f29 - func_00344720(sp300);
        temp_f21 = temp_f27 - func_00344748(sp300);
        var_f22 = (temp_f22_2 + 0x1.618bf00000000p+2f) / ((temp_f25_2 * temp_f20_3) + (temp_f24_2 * temp_f21) + (temp_f23_2 * (temp_f26 - func_00344770(sp300))) + 0x1.618bf00000000p+2f);
    }
    temp_v1_4 = M2C_FIELD(arg1, char **, 0) + 0x108;
    var_s3 = 4;
    temp_v0_9 = M2C_FIELD(temp_v1_4, s32 (**)(char *), 4)(arg1 + M2C_FIELD(temp_v1_4, s16 *, 0));
    if (temp_v0_9 >= 0) {
        if (temp_v0_9 >= 2) {
            if (temp_v0_9 != 2) {

            } else {
                var_s3 = (packet.sp0 == 0) ? 3 : 2;
            }
        } else {
            var_s3 = 1;
            if (packet.sp0 != 0) {
                var_s3 = 0;
                vec.l[1] = M2C_FIELD((char *)D_006A2580, s64 *, 8);
                vec.l[0] = M2C_FIELD((char *)D_006A2580, s64 *, 0);
            }
        }
    }
    sp2E0 = M2C_FIELD(temp_s4, u16 *, 0x4FA);
    sp2E4 = 0x0.0p+0f;
    Vec *pos1 = (Vec *)&packet.sp10;
    *pos1 = vec;
    *(Vec *)&packet.sp20 = vec;
    *(Vec *)&packet.sp30 = vec;
    if (var_s3 == 0) {
        Vec *pos2 = (Vec *)&packet.sp20;
        Vec *pos3 = (Vec *)&packet.sp30;
        temp_v0_10 = M2C_FIELD(arg1, char **, 0) + 0x128;
        M2C_FIELD(&packet.sp18, float *, 0) = M2C_FIELD(&packet.sp18, float *, 0) + -0x1.4000000000000p+0f;
        M2C_FIELD(&packet.sp28, float *, 0) = M2C_FIELD(&packet.sp28, float *, 0) + -0x1.47ae140000000p-7f;
        M2C_FIELD(&packet.sp38, float *, 0) = M2C_FIELD(&packet.sp38, float *, 0) + 0x1.4000000000000p+0f;
        temp_f20_4 = -M2C_FIELD(temp_v0_10, f32 (**)(char *), 4)(arg1 + M2C_FIELD(temp_v0_10, s16 *, 0));
        func_00391C28(pos1, temp_f20_4);
        func_00391C28(pos2, temp_f20_4);
        func_00391C28(pos3, temp_f20_4);
    }
    int *ids = &packet.sp40;
    temp_v0_11 = func_003FA910(sp300);
    *ids = remap(temp_v0_11) - 1;
    packet.sp48 = 2;
    packet.sp44 = 1;
    temp_f0_2 = func_003F3A68(var_s3, 0);
    packet.sp4C = temp_f0_2 * (f32) M2C_FIELD(temp_s4, u8 *, 0x604) * 0x1.0000000000000p-6f;
    packet.sp50 = var_f22;
    packet.sp54 = (f32) sp2E0;
    packet.sp64 = func_003F3A68(var_s3, 2) * (f32) M2C_FIELD(temp_s4, u8 *, 0x606) * 0x1.0000000000000p-7f;
    packet.sp68 = (f32) M2C_FIELD(temp_s4, u16 *, 0x4F8) / 0x1.8000000000000p+12f;
    temp_f20_5 = func_003F3A68(var_s3, 4);
    packet.sp6C = temp_f20_5 * func_0034FED0(sp300, &sp2E4);
    packet.sp70 = var_f22 * sp2E4;
    temp_f0_3 = func_003F3A68(var_s3, 1);
    packet.sp74 = temp_f0_3 * (f32) M2C_FIELD(temp_s4, u8 *, 0x605) * 0x1.0000000000000p-6f;
    packet.sp78 = var_f22;
    packet.sp7C = (f32) sp2E0;
    temp_f20_6 = func_003F3A68(var_s3, 7);
    packet.sp80 = temp_f20_6 * func_0034FBC0(sp300, &sp2E4);
    packet.sp84 = var_f22 * sp2E4;
    temp_f20_7 = func_003F3A68(var_s3, 8);
    temp_f20_8 = temp_f20_7 * func_0034FA90(sp300, &sp2E0);
    var_f21 = 0x0.0p+0f;
    packet.sp5C = var_f22;
    packet.sp88 = 0x0.0p+0f;
    packet.sp8C = var_f22;
    packet.sp58 = temp_f20_8;
    packet.sp60 = (f32) sp2E0;
    if ((var_s3 == 0) && (func_0057F6F8(bits_to_double(func_0057FB50(func_00354EB0(sp300))), 0x0.0p+0) != 0)) {
        var_f21 = 0x1.0000000000000p+0f;
    }
    M2C_FIELD(&packet.sp90, f32 *, 0) = var_f21;
    M2C_FIELD(&packet.sp90, f32 *, 4) = 0x1.0000000000000p+0f;
    packet.sp2B0 = 0;
    packet.sp2B4 = 0;
    packet.sp2B8 = 0;
    temp_a0 = func_003B6B78(arg0, 0);
    packet.sp2C0 = 0;
    packet.sp2C4 = 0;
    if ((sp2F8 == 0) && (temp_a0 != 0)) {
        temp_a0_2 = (func_00579358(temp_a0) & 1) + 1;
        if (temp_a0_2 != 1) {
            packet.sp2C0 = 0;
        } else {
            packet.sp2C0 = 3.0f;
        }
        if (temp_a0_2 != 2) {
            packet.sp2C4 = 0;
        } else {
            packet.sp2C4 = 3.0f;
        }
    }
    var_f20 = 0x0.0p+0f;
    if (var_s3 == 0) {
        var_s0 = 0;
        do {
            temp_a1_3 = var_s0;
            var_s0 += 1;
            float v = func_003FA7B8(sp300, temp_a1_3);
            __asm__("max.s %0,%1,%2" : "=f"(var_f20) : "f"(var_f20), "f"(v));
        } while (var_s0 < 4);
        var_f20 = var_f20 * (func_003F3A68(var_s3, 0xF) * 0x1.4000000000000p+1f);
    }
    var_f0_2 = 0x0.0p+0f;
    packet.sp2BC = var_f20;
    if (var_s3 == 0) {
        var_f0_2 = func_00355950(sp300);
    }
    packet.sp2C8 = var_f0_2;
    packet.sp2A0 = func_003F3A68(var_s3, 3) * M2C_FIELD(temp_s4, f32 *, 0x504);
    temp_f20_9 = func_003F3A68(var_s3, 5);
    packet.sp2A4 = 0x0.0p+0f;
    packet.sp2A8 = 0x0.0p+0f;
    packet.sp2AC = 0x0.0p+0f;
    if (packet.sp0 != 0) {
        if (sp2F4 == 0) {
            temp_f20_10 = temp_f20_9 * func_00350500(sp300, &sp2E8, &sp2EC, &sp2F0);
            switch (sp2EC) {                        /* irregular */
            case 0:
                packet.sp2A4 = temp_f20_10;
                break;
            case 1:
                packet.sp2A8 = temp_f20_10;
                break;
            case 2:
                packet.sp2AC = temp_f20_10;
                break;
            }
        }
    }
    func_003F3A68(var_s3, 0xC);
    temp_f31 = func_003F3A68(var_s3, 0xB);
    temp_f30_2 = func_003F3A68(var_s3, 0xE);
    temp_f29_2 = func_003F3A68(var_s3, 6);
    temp_f28 = func_003F3A68(var_s3, 0xD);
    temp_f27_2 = func_003F3A68(var_s3, 9);
    temp_f26_2 = func_003F3A68(var_s3, 0xA);
    temp_f25_3 = func_003F3A68(var_s3, 6);
    temp_f24_3 = func_003F3A68(var_s3, 6);
    temp_f23_3 = func_003F3A68(var_s3, 6);
    temp_f21_2 = func_003F3A68(var_s3, 0xE);
    temp_f20_11 = func_003F3A68(var_s3, 0xE);
    func_003F3A68(var_s3, 0xE);
    sp304 = pv->l;
    var_s7 = 0;
    var_fp = (char *)&packet;
    do {
        temp_s2 = var_fp + 0xDC;
        temp_s3 = var_fp + 0xFC;
        temp_s4_2 = var_fp + 0xE4;
        temp_s0_2 = var_fp + 0xCC;
        func_00344FC0(sp300, sp304, var_s7, 0);
        temp_s1 = var_fp + 0xD4;
        vec.f[3] = 0x1.0000000000000p+0f;
        temp_s5 = var_fp + 0x10C;
        temp_s6 = var_fp + 0x114;
        func_004A7AA8(sp304, sp304);
        temp_a4 = var_fp + 0xA0;
        temp_v1_5 = var_fp + 0xC4;
        M2C_FIELD(var_fp, s64 *, 0xA0) = vec.l[0];
        M2C_FIELD(temp_a4, s64 *, 8) = vec.l[1];
        temp_v0_12 = var_fp + 0xF4;
        M2C_FIELD(var_fp, s32 *, 0xC4) = 0;
        M2C_FIELD(temp_v1_5, f32 *, 4) = var_f22;
        temp_v1_6 = var_fp + 0xEC;
        sp308 = temp_v1_6;
        M2C_FIELD(var_fp, s32 *, 0xDC) = 0;
        sp30C = temp_v0_12;
        M2C_FIELD(temp_s2, f32 *, 4) = var_f22;
        M2C_FIELD(var_fp, s32 *, 0xFC) = 0;
        M2C_FIELD(temp_s3, f32 *, 4) = var_f22;
        M2C_FIELD(var_fp, s32 *, 0xE4) = 0;
        M2C_FIELD(temp_s4_2, f32 *, 4) = var_f22;
        M2C_FIELD(var_fp, s32 *, 0xCC) = 0;
        M2C_FIELD(temp_s0_2, f32 *, 4) = var_f22;
        M2C_FIELD(var_fp, s32 *, 0xD4) = 0;
        M2C_FIELD(temp_s1, f32 *, 4) = var_f22;
        func_0035FFB0(sp300, var_s7, temp_v1_5, var_fp + 0xC8, temp_f31, var_f22);
        func_00360028(sp300, var_s7, temp_s0_2, var_fp + 0xD0, temp_f27_2, var_f22);
        func_003600E8(sp300, var_s7, temp_s1, var_fp + 0xD8, temp_f26_2, var_f22);
        func_00360158(sp300, var_s7, temp_s2, var_fp + 0xE0, temp_f30_2, var_f22);
        func_003601A8(sp300, var_s7, temp_s3, var_fp + 0x100, temp_f29_2, var_f22);
        func_00360200(sp300, var_s7, temp_s4_2, var_fp + 0xE8, temp_f28, var_f22);
        temp_v0_13 = var_fp + 0x104;
        M2C_FIELD(var_fp, s32 *, 0x104) = 0;
        M2C_FIELD(temp_v0_13, f32 *, 4) = var_f22;
        M2C_FIELD(var_fp, s32 *, 0x10C) = 0;
        M2C_FIELD(temp_s5, f32 *, 4) = var_f22;
        M2C_FIELD(var_fp, s32 *, 0x114) = 0;
        M2C_FIELD(temp_s6, f32 *, 4) = var_f22;
        M2C_FIELD(var_fp, s32 *, 0xEC) = 0;
        M2C_FIELD(temp_v1_6, f32 *, 4) = var_f22;
        M2C_FIELD(var_fp, s32 *, 0xF4) = 0;
        M2C_FIELD(temp_v0_12, f32 *, 4) = var_f22;
        func_003602A0(sp300, var_s7, temp_v0_13, var_fp + 0x108, temp_f25_3, var_f22);
        func_003602C8(sp300, var_s7, temp_s5, var_fp + 0x110, temp_f24_3, var_f22);
        func_003602F0(sp300, var_s7, temp_s6, var_fp + 0x118, temp_f23_3, var_f22);
        func_003602F8(sp300, var_s7, sp308, var_fp + 0xF0, temp_f21_2, var_f22);
        temp_a1_4 = var_s7;
        var_s7 += 1;
        func_00360320(sp300, temp_a1_4, sp30C, var_fp + 0xF8, temp_f20_11, var_f22);
        var_fp += 0x80;
    } while (var_s7 < 4);
    func_00392640(sp2FC, (char *)&packet);
}
