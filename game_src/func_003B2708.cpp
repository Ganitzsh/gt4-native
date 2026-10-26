/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native correction of the provisional draft of 003B2708: 003B1FA8 and 003B21D8 take five arguments
 * (self, camera, &x, &y, &z); the original passes &sp640 in $t0, the draft passed four (natively
 * the fifth read 0: a write through a null pointer in the race). */
#include "types.h"
#include "m2c_macros.h"
#define NULL 0
#define F(p,t,o) (*(t *)((char *)(p)+(o)))
#define Q(x) (*(s128 *)&(x))
static inline float ee_min(float a,float b){float r;asm("min.s %0, %1, %2":"=f"(r):"f"(a),"f"(b));return r;}
static inline float ee_max(float a,float b){float r;asm("max.s %0, %1, %2":"=f"(r):"f"(a),"f"(b));return r;}
#define sp18 F(stack,f32,0x8)
#define sp1C F(stack,f32,0xc)
#define sp60 F(stack,f32,0x50)
#define sp64 F(stack,f32,0x54)
#define sp6C F(stack,f32,0x5c)
#define sp74 F(stack,f32,0x64)
#define sp78 F(stack,f32,0x68)
#define sp7C F(stack,f32,0x6c)
#define sp84 F(stack,f32,0x74)
#define spA0 F(stack,f32,0x90)
#define spA4 F(stack,f32,0x94)
#define spA8 F(stack,f32,0x98)
#define spCC F(stack,s32,0xbc)
#define spD0 F(stack,s32,0xc0)
#define spD8 F(stack,s32,0xc8)
#define spDC F(stack,s32,0xcc)
#define sp210 F(stack,s32,0x200)
#define sp220 F(stack,f32,0x210)
#define sp224 F(stack,f32,0x214)
#define sp228 F(stack,f32,0x218)
#define sp22C F(stack,f32,0x21c)
#define sp230 F(stack,f32,0x220)
#define sp234 F(stack,f32,0x224)
#define sp238 F(stack,f32,0x228)
#define sp23C F(stack,f32,0x22c)
#define sp240 F(stack,f32,0x230)
#define sp244 F(stack,f32,0x234)
#define sp248 F(stack,f32,0x238)
#define sp24C F(stack,f32,0x23c)
#define sp250 F(stack,f32,0x240)
#define sp254 F(stack,f32,0x244)
#define sp258 F(stack,f32,0x248)
#define sp25C F(stack,f32,0x24c)
#define sp260 F(stack,f32,0x250)
#define sp264 F(stack,f32,0x254)
#define sp268 F(stack,f32,0x258)
#define sp26C F(stack,f32,0x25c)
#define sp270 F(stack,f32,0x260)
#define sp274 F(stack,f32,0x264)
#define sp278 F(stack,f32,0x268)
#define sp27C F(stack,f32,0x26c)
#define sp290 F(stack,s128,0x280)
#define sp2A0 F(stack,s128,0x290)
#define sp2B8 F(stack,f32,0x2a8)
#define sp2BC F(stack,f32,0x2ac)
#define sp2D0 F(stack,s128,0x2c0)
#define sp2E0 F(stack,f32,0x2d0)
#define sp2E4 F(stack,s32,0x2d4)
#define sp2F0 F(stack,s32,0x2e0)
#define sp2F4 F(stack,s32,0x2e4)
#define sp300 F(stack,s128,0x2f0)
#define sp310 F(stack,s128,0x300)
#define sp320 F(stack,s128,0x310)
#define sp330 F(stack,f32,0x320)
#define sp334 F(stack,f32,0x324)
#define sp350 F(stack,s128,0x340)
#define sp360 F(stack,f32,0x350)
#define sp370 F(stack,f32,0x360)
#define sp374 F(stack,f32,0x364)
#define sp378 F(stack,f32,0x368)
#define sp380 F(stack,s128,0x370)
#define sp390 F(stack,s128,0x380)
#define sp3A0 F(stack,s128,0x390)
#define sp3B0 F(stack,s128,0x3a0)
#define sp3C0 F(stack,s128,0x3b0)
#define sp3D0 F(stack,s128,0x3c0)
#define sp3E0 F(stack,s128,0x3d0)
#define sp3F0 F(stack,s128,0x3e0)
#define sp400 F(stack,s128,0x3f0)
#define sp410 F(stack,s128,0x400)
#define sp450 F(stack,f32,0x440)
#define sp490 F(stack,s32,0x480)
#define sp494 F(stack,f32,0x484)
#define sp498 F(stack,f32,0x488)
#define sp4A0 F(stack,f32,0x490)
#define sp4B0 F(stack,f32,0x4a0)
#define sp4B4 F(stack,f32,0x4a4)
#define sp4B8 F(stack,f32,0x4a8)
#define sp4C0 F(stack,int,0x4b0)
#define sp5C0 F(stack,int,0x5b0)
#define sp5D0 F(stack,int,0x5c0)
#define sp630 F(stack,f32,0x620)
#define sp634 F(stack,f32,0x624)
#define sp640 F(stack,f32,0x630)
#define sp644 F(stack,f32,0x634)
#define sp650 F(stack,f32,0x640)
#define sp654 F(stack,f32,0x644)
#define sp658 F(stack,f32,0x648)
#define sp660 F(stack,f32,0x650)
#define sp664 F(stack,f32,0x654)
#define sp668 F(stack,f32,0x658)
#define sp670 F(stack,f32,0x660)
#define sp674 F(stack,f32,0x664)
#define sp678 F(stack,f32,0x668)
#define sp680 F(stack,f32,0x670)
#define sp684 F(stack,f32,0x674)
#define sp688 F(stack,f32,0x678)
#define sp68C F(stack,f32,0x67c)
#define sp690 F(stack,s128,0x680)
#define sp6A0 F(stack,f32,0x690)
#define sp6A4 F(stack,f32,0x694)
#define sp6A8 F(stack,f32,0x698)
#define sp6B0 F(stack,f32,0x6a0)
#define sp6B4 F(stack,f32,0x6a4)
#define sp6B8 F(stack,f32,0x6a8)
#define sp6C0 F(stack,s32,0x6b0)
#define sp6D0 F(stack,s128,0x6c0)
#define sp6E0 F(stack,f32,0x6d0)
#define sp6E4 F(stack,f32,0x6d4)
#define sp6E8 F(stack,f32,0x6d8)
#define sp6F0 F(stack,f32,0x6e0)
#define sp6F4 F(stack,f32,0x6e4)
#define sp6F8 F(stack,f32,0x6e8)
#define sp700 F(stack,f32,0x6f0)
#define sp704 F(stack,f32,0x6f4)
#define sp708 F(stack,f32,0x6f8)
#define sp280 F(stack,f32,0x270)
#define sp2E8 F(stack,s32,0x2d8)
extern "C" {
f32 func_00344798(s32, f32);                        /* extern */
f32 func_003447F8(s32, f32);                        /* extern */
f32 func_00344828(s32, f32);                        /* extern */
f32 func_00344858(s32, f32);                        /* extern */
f32 func_00344888(s32, f32);                        /* extern */
f32 func_003448B8(s32, f32);                        /* extern */
s32 func_0035BBD8(s32);                             /* extern */
int func_00385470(void *, void *);        /* extern */
f32 func_00385688(void *);                       /* extern */
f32 func_00385698(void *);                       /* extern */
f32 func_003856A8(void *);                       /* extern */
f32 func_003856B8(void *);                       /* extern */
f32 func_003856C8(void *);                       /* extern */
f32 func_003856D8(void *);                       /* extern */
f32 func_003856E8(void *);                       /* extern */
f32 func_00385700(void *);                       /* extern */
f32 func_00385738(void *);                       /* extern */
f32 func_00385748(void *);                       /* extern */
f32 func_00385758(void *);                       /* extern */
f32 func_00385778(void *);                       /* extern */
f32 func_00385788(void *);                       /* extern */
f32 func_003857A8(void *);                       /* extern */
f32 func_003857C0(void *);                       /* extern */
f32 func_003857E8(void *);                       /* extern */
f32 func_00385800(void *);                       /* extern */
f32 func_00385828(void *);                       /* extern */
f32 func_00385840(void *);                       /* extern */
f32 func_00385858(void *);                       /* extern */
f32 func_00385868(void *);                       /* extern */
f32 func_00385878(void *);                       /* extern */
f32 func_00385888(void *);                       /* extern */
f32 func_00385898(void *);                       /* extern */
f32 func_003858A8(void *);                       /* extern */
f32 func_003858B8(void *);                       /* extern */
f32 func_003858C8(void *);                       /* extern */
f32 func_003858D8(void *);                       /* extern */
f32 func_003858E8(void *);                       /* extern */
f32 func_003858F8(void *);                       /* extern */
f32 func_00385908(void *);                       /* extern */
f32 func_00385918(void *);                       /* extern */
f32 func_00385928(void *);                       /* extern */
f32 func_00385938(void *);                       /* extern */
f32 func_00385948(void *);                       /* extern */
f32 func_00385958(void *);                       /* extern */
f32 func_00385968(void *);                       /* extern */
f32 func_00385998(void *);                       /* extern */
f32 func_003859A8(void *);                       /* extern */
f32 func_003859B8(void *);                       /* extern */
f32 func_003859C8(void *);                       /* extern */
f32 func_003859D8(void *);                       /* extern */
f32 func_003859E8(void *);                       /* extern */
f32 func_003859F8(void *);                       /* extern */
f32 func_00385A08(void *);                       /* extern */
f32 func_00385A18(void *);                       /* extern */
f32 func_00385A38(void *);                       /* extern */
f32 func_00385A48(void *);                       /* extern */
f32 func_00385A58(void *);                       /* extern */
f32 func_00385A68(void *);                       /* extern */
f32 func_00385A88(void *);                       /* extern */
f32 func_00385A98(void *);                       /* extern */
f32 func_00385AA8(void *);                       /* extern */
f32 func_00385AC0(void *);                       /* extern */
f32 func_00385AD0(void *);                       /* extern */
int func_00386818(void *, void *, int, f32); /* extern */
char *func_0038DC18(void *);                  /* extern */
int func_003B05A8(void *);                   /* extern */
int func_003B1DB0(s32, void *, void *, s32) throw(); /* extern */
int func_003B1FA8(void *, void *, void *, void *, void *) throw();
int func_003B21D8(void *, void *, void *, void *, void *) throw();
int func_003B2370(void *, void *, void *, void *, f32) throw(); /* extern */
int func_003B2440(void *, void *, void *, f32) throw();    /* extern */
int func_003B2508(void *, s32, int, int, f32, f32, f32, f32, f32, f32, f32, f32, f32) throw(); /* extern */
s32 func_00402270(void *, s32);                    /* extern */
int func_00415240(void *, void *);                /* extern */
int func_004152B0(void *, int, void *);       /* extern */
int func_00415350(void *);                       /* extern */
int func_00416B58(void *);                   /* extern */
int func_00416F88(void *);                   /* extern */
int func_00416FD8(void *, void *, void *, void *); /* extern */
int func_00421DF8(void *, void *, int, f32, f32); /* extern */
s32 func_00459178(void *);                          /* extern */
f32 func_004860E8(int, int, f32);           /* extern */
int func_00488528(void *, f32, f32, f32);        /* extern */
int func_0048D530(void *, void *, void *);        /* extern */
int func_0057D8A0(void *, void *, f32);           /* extern */
int func_005A48D8(void *, int, int);     /* extern */

}
extern "C" s32 func_003B2708(char *arg0, char **arg1, s32 arg2, s32 arg3, char *arg4, int arg5, int arg6, f32 fparg0) {
    char stack[0x700] __attribute__((aligned(16)));
    s32 sp710;
    char *sp714;
    s32 sp718;
    char *sp71C;
    s32 sp720;
    s32 sp724;
    s32 sp728;
    s32 sp72C;
    s32 sp730;
    char *temp_v1;
    int var_s0;
    int var_s6;
    f32 (*temp_a1_2)(void *);
    f32 temp_f0;
    f32 temp_f0_10;
    f32 temp_f0_11;
    f32 temp_f0_12;
    f32 temp_f0_13;
    f32 temp_f0_14;
    f32 temp_f0_15;
    f32 temp_f0_16;
    f32 temp_f0_17;
    f32 temp_f0_18;
    f32 temp_f0_19;
    f32 temp_f0_20;
    f32 temp_f0_21;
    f32 temp_f0_22;
    f32 temp_f0_23;
    f32 temp_f0_24;
    f32 temp_f0_25;
    f32 temp_f0_26;
    f32 temp_f0_27;
    f32 temp_f0_28;
    f32 temp_f0_29;
    f32 temp_f0_2;
    f32 temp_f0_30;
    f32 temp_f0_31;
    f32 temp_f0_32;
    f32 temp_f0_33;
    f32 temp_f0_34;
    f32 temp_f0_35;
    f32 temp_f0_36;
    f32 temp_f0_37;
    f32 temp_f0_3;
    f32 temp_f0_4;
    f32 temp_f0_5;
    f32 temp_f0_6;
    f32 temp_f0_7;
    f32 temp_f0_8;
    f32 temp_f0_9;
    f32 temp_f1;
    f32 temp_f1_2;
    f32 temp_f1_3;
    f32 temp_f1_4;
    f32 temp_f1_5;
    f32 temp_f1_6;
    f32 temp_f1_7;
    f32 temp_f1_8;
    f32 temp_f1_9;
    f32 temp_f20;
    f32 temp_f20_10;
    f32 temp_f20_11;
    f32 temp_f20_12;
    f32 temp_f20_13;
    f32 temp_f20_14;
    f32 temp_f20_15;
    f32 temp_f20_16;
    f32 temp_f20_17;
    f32 temp_f20_18;
    f32 temp_f20_19;
    f32 temp_f20_20;
    f32 temp_f20_21;
    f32 temp_f20_22;
    f32 temp_f20_23;
    f32 temp_f20_2;
    f32 temp_f20_3;
    f32 temp_f20_4;
    f32 temp_f20_5;
    f32 temp_f20_6;
    f32 temp_f20_7;
    f32 temp_f20_8;
    f32 temp_f20_9;
    f32 temp_f21;
    f32 temp_f21_10;
    f32 temp_f21_11;
    f32 temp_f21_12;
    f32 temp_f21_13;
    f32 temp_f21_14;
    f32 temp_f21_15;
    f32 temp_f21_16;
    f32 temp_f21_2;
    f32 temp_f21_3;
    f32 temp_f21_4;
    f32 temp_f21_5;
    f32 temp_f21_6;
    f32 temp_f21_7;
    f32 temp_f21_8;
    f32 temp_f21_9;
    f32 temp_f22;
    f32 temp_f23;
    f32 temp_f25;
    f32 temp_f26;
    f32 temp_f2;
    f32 temp_f2_2;
    f32 temp_f3;
    f32 temp_f5;
    f32 var_f0;
    f32 var_f0_2;
    f32 var_f0_3;
    f32 var_f0_4;
    f32 var_f1;
    f32 var_f1_2;
    f32 var_f1_3;
    f32 var_f20;
    f32 var_f20_2;
    f32 var_f22;
    f32 var_f23;
    f32 var_f23_2;
    f32 var_f24;
    f32 var_f24_2;
    f32 var_f27;
    f32 var_f3;
    s128 temp_v0_34;
    s32 temp_a0_3;
    s32 temp_a1;
    s32 temp_s0_2;
    s32 temp_s3;
    s32 temp_s5;
    s32 temp_s7_2;
    s32 temp_v0;
    s32 var_v0;
    u8 temp_a0;
    char *temp_a0_2;
    char *temp_s0;
    char *temp_s7;
    char *temp_v0_10;
    char *temp_v0_11;
    char *temp_v0_12;
    char *temp_v0_13;
    char *temp_v0_14;
    char *temp_v0_15;
    char *temp_v0_16;
    char *temp_v0_17;
    char *temp_v0_18;
    char *temp_v0_19;
    char *temp_v0_20;
    char *temp_v0_21;
    char *temp_v0_22;
    char *temp_v0_23;
    char *temp_v0_24;
    char *temp_v0_25;
    char *temp_v0_26;
    char *temp_v0_27;
    char *temp_v0_28;
    char *temp_v0_29;
    char *temp_v0_2;
    char *temp_v0_30;
    char *temp_v0_31;
    char *temp_v0_32;
    char *temp_v0_33;
    char *temp_v0_3;
    char *temp_v0_4;
    char *temp_v0_5;
    char *temp_v0_6;
    char *temp_v0_7;
    char *temp_v0_8;
    char *temp_v0_9;
    char *temp_v1_10;
    char *temp_v1_11;
    char *temp_v1_12;
    char *temp_v1_2;
    char *temp_v1_3;
    char *temp_v1_4;
    char *temp_v1_5;
    char *temp_v1_6;
    char *temp_v1_7;
    char *temp_v1_8;
    char *temp_v1_9;

    temp_s3 = arg6;
    M2C_FIELD(arg0, s32 *, 0x6BC) = 1;
    sp710 = arg3;
    sp714 = (char *)arg4;
    func_00416B58(stack);
    sp718 = arg2 + 0x9A0;
    func_00415350(&sp370);
    temp_s7 = (char *)sp718 + ((sp710 == 0) * 0x370);
    if (M2C_FIELD(temp_s7, s32 *, 0x368) != 0) {
        if (func_0038DC18(arg1) != NULL) {
            temp_a1 = temp_s3 != 0;
            temp_s5 = ((M2C_FIELD(func_0038DC18(arg1), u8 *, 2) ^ 0x11) == 0) ? temp_a1 : 0;
            temp_s0 = M2C_FIELD(func_0038DC18(arg1), char **, 0x50);
            func_005A48D8(&sp4B0, 0, 0xC);
            if (temp_s0 != NULL) {
                sp4B0 = M2C_FIELD(temp_s0, f32 *, 0x34);
                sp4B4 = M2C_FIELD(temp_s0, f32 *, 0x38);
                sp4B8 = M2C_FIELD(temp_s0, f32 *, 0x3C);
            }
            var_s0 = 3;
            temp_a0 = M2C_FIELD(func_0038DC18(arg1), u8 *, 2);
            var_s6 = 0;
            switch (temp_a0) {                      /* switch 1; irregular */
            case 5:                                 /* switch 1 */
                if (temp_s3 != 0) {
                    var_s0 = 2;
                } else if (temp_a0 == 0x11) {
                case 17:                            /* switch 1 */
                    var_s0 = ((u32) (func_0035BBD8(arg2) - 2) < 4U) ? 0 : 3;
                }
                break;
            }
            if (arg5 != 0) {
                var_s6 = 2;
                var_s0 = 3;
            }
            temp_f0 = func_00344798(arg2, fparg0);
            sp71C = arg0 + 0x10;
            temp_f23 = func_003447F8(arg2, fparg0);
            temp_f22 = func_00344828(arg2, fparg0);
            temp_f21 = func_00344858(arg2, fparg0);
            temp_f20 = func_003448B8(arg2, fparg0);
            func_003B2508(arg0, temp_s5, var_s0, var_s6, temp_f0, temp_f23, temp_f22, temp_f21, temp_f20, -func_00344888(arg2, fparg0), sp4B0, sp4B4, sp4B8);
            temp_s0_2 = M2C_FIELD(sp71C, s32 *, 0x690);
            if (((temp_s3 == 0) || (var_v0 = 0, (temp_s0_2 != 3))) && (var_v0 = 0, (M2C_FIELD(arg0, s32 *, 0x7C0) <= 0))) {
                sp720 = 1;
                temp_a0_2 = func_0038DC18(arg1);
                sp724 = 1;
                temp_v0 = func_00459178(temp_a0_2);
                temp_a0_3 = temp_v0;
                sp728 = 1;
                if (temp_s3 != 0) {
                    switch (temp_a0_3) {            /* switch 2; irregular */
                    case 0:                         /* switch 2 */
                        M2C_FIELD(arg0, s32 *, 0x6BC) = 0;
                        return temp_v0;
                    case 1:                         /* switch 2 */
                        sp720 = 0;
                        spD8 = sp728;
                        spCC = 0;
                        sp724 = 0;
                        spD0 = 0;
                        goto block_41;
                    case 2:                         /* switch 2 */
                        spD0 = 0;
                        spD8 = sp728;
                        if (((temp_s5 > 0) && (temp_s0_2 > 0)) || (temp_s0_2 >= 3)) {
                            spCC = 0;
                            sp728 = 0;
                        } else {
                            spDC = sp728;
                        }
                        sp724 = 0;
                        sp720 = 1;
                        goto block_41;
                    case 3:                         /* switch 2 */
                        sp720 = 1;
                        sp724 = 1;
                        goto block_41;
                    }
                } else {
block_41:
                    var_f22 = 0x0.0p+0f;
                    func_00386818(&sp4C0, temp_s7, 1, fparg0);
                    sp730 = 1;
                    func_00385470(&sp5C0, &sp4C0);
                    var_f24 = 0x0.0p+0f;
                    sp72C = 0;
                    func_003B05A8(&sp5D0);
                    var_f23 = 0x0.0p+0f;
                    temp_s7_2 = func_00402270(arg1, sp718 + 0x6E0);
                    sp2E4 = (sp710 == 0) ? sp730 : sp2E4;
                    if (temp_s7_2 == sp730) {
                        func_00416F88(stack);
                        var_f22 = (func_003856D8(&sp5C0) * 0x1.9999980000000p-7f) + 0x0.0p+0f;
                        temp_f0_2 = func_003856D8(&sp5C0);
                        temp_f1 = -0x1.41b2f40000000p-2f;
                        sp640 = -0x1.9999980000000p-4f;
                        sp644 = 0x1.9999980000000p-4f;
                        temp_f3 = 0x1.0000000000000p+0f / (0x1.41b2f40000000p-2f - temp_f1);
                        sp630 = temp_f3 * -0x1.9999980000000p-3f;
                        sp634 = temp_f3 * ((0x1.41b2f40000000p-2f * 0x1.9999980000000p-4f) - (temp_f1 * -0x1.9999980000000p-4f));
                        var_f23 = (temp_f0_2 * -0x1.9999980000000p-5f) + 0x0.0p+0f + ee_min(ee_max((sp630 * func_003856B8(&sp5C0)) + sp634, sp640), sp644);
                    }
                    if (temp_s7_2 == 3) {
                        sp2F4 = sp730;
                        var_f24 = func_00385968(&sp5C0) + 0x0.0p+0f;
                        sp27C = 0x1.921fb40000000p+0f;
                    }
                    if (temp_s7_2 == 4) {
                        sp2F4 = 2;
                        var_f24 += func_00385968(&sp5C0);
                    }
                    temp_v1 = (sp714 == NULL) ? (char *)&sp5D0 : sp714;
                    sp714 = temp_v1;
                    func_003B1DB0(temp_s7_2, temp_v1, &sp5C0, arg2);
                    if (func_003856C8(&sp5C0) > 0x1.47ae140000000p-7f) {
                        var_f20 = 0x1.47ae140000000p-7f;
                        var_f22 += func_003856C8(&sp5C0) - 0x1.47ae140000000p-7f;
                    } else {
                        var_f20 = func_003856C8(&sp5C0) + 0x0.0p+0f;
                    }
                    temp_v1_2 = *arg1;
                    sp650 = 0x0.0p+0f;
                    sp654 = var_f20;
                    sp658 = 0x0.0p+0f;
                    sp2D0 = Q(sp650);
                    temp_v0_2 = *arg1;
                    temp_f21_2 = M2C_FIELD((temp_v1_2 + 0x40), f32 (**)(char *, int, f32), 4)((char *)arg1 + M2C_FIELD(temp_v1_2, s16 *, 0x40), 0, fparg0);
                    temp_v0_3 = *arg1;
                    temp_f20_2 = M2C_FIELD((temp_v0_2 + 0x48), f32 (**)(char *, int, f32), 4)((char *)arg1 + M2C_FIELD(temp_v0_2, s16 *, 0x48), 0, fparg0);
                    temp_f0_3 = M2C_FIELD((temp_v0_3 + 0x50), f32 (**)(char *, int, f32), 4)((char *)arg1 + M2C_FIELD(temp_v0_3, s16 *, 0x50), 0, fparg0);
                    sp650 = temp_f21_2;
                    sp654 = temp_f20_2;
                    sp658 = temp_f0_3;
                    temp_v1_3 = *arg1;
                    sp660 = -sp650;
                    sp664 = temp_f20_2;
                    sp668 = -sp658;
                    sp390 = Q(sp660);
                    temp_v0_4 = *arg1;
                    temp_f21_3 = M2C_FIELD((temp_v1_3 + 0x40), f32 (**)(char *, int, f32), 4)((char *)arg1 + M2C_FIELD(temp_v1_3, s16 *, 0x40), 1, fparg0);
                    temp_v0_5 = *arg1;
                    temp_f20_3 = M2C_FIELD((temp_v0_4 + 0x48), f32 (**)(char *, int, f32), 4)((char *)arg1 + M2C_FIELD(temp_v0_4, s16 *, 0x48), 1, fparg0);
                    temp_f0_4 = M2C_FIELD((temp_v0_5 + 0x50), f32 (**)(char *, int, f32), 4)((char *)arg1 + M2C_FIELD(temp_v0_5, s16 *, 0x50), 1, fparg0);
                    sp650 = temp_f21_3;
                    sp654 = temp_f20_3;
                    sp658 = temp_f0_4;
                    temp_v1_4 = *arg1;
                    sp660 = -sp650;
                    sp664 = sp654;
                    sp668 = -temp_f0_4;
                    sp3A0 = Q(sp660);
                    temp_v0_6 = *arg1;
                    temp_f21_4 = M2C_FIELD((temp_v1_4 + 0x10), f32 (**)(char *, f32), 4)((char *)arg1 + M2C_FIELD(temp_v1_4, s16 *, 0x10), fparg0);
                    temp_v0_7 = *arg1;
                    temp_f20_4 = M2C_FIELD((temp_v0_6 + 0x18), f32 (**)(char *, f32), 4)((char *)arg1 + M2C_FIELD(temp_v0_6, s16 *, 0x18), fparg0) + var_f22;
                    temp_f0_5 = M2C_FIELD((temp_v0_7 + 0x20), f32 (**)(char *, f32), 4)((char *)arg1 + M2C_FIELD(temp_v0_7, s16 *, 0x20), fparg0);
                    sp650 = temp_f21_4;
                    temp_f0_6 = temp_f0_5 + var_f23;
                    sp654 = temp_f20_4;
                    temp_v1_5 = *arg1;
                    sp658 = temp_f0_6;
                    sp660 = -sp650;
                    sp664 = sp654;
                    sp668 = -temp_f0_6;
                    sp3B0 = Q(sp660);
                    temp_v0_8 = *arg1;
                    temp_f21_5 = M2C_FIELD((temp_v1_5 + 0x58), f32 (**)(char *, f32), 4)((char *)arg1 + M2C_FIELD(temp_v1_5, s16 *, 0x58), fparg0);
                    temp_v0_9 = *arg1;
                    temp_f20_5 = M2C_FIELD((temp_v0_8 + 0x60), f32 (**)(char *, f32), 4)((char *)arg1 + M2C_FIELD(temp_v0_8, s16 *, 0x60), fparg0);
                    temp_f0_7 = M2C_FIELD((temp_v0_9 + 0x68), f32 (**)(char *, f32), 4)((char *)arg1 + M2C_FIELD(temp_v0_9, s16 *, 0x68), fparg0);
                    sp650 = temp_f21_5;
                    sp654 = temp_f20_5;
                    sp658 = temp_f0_7;
                    temp_v1_6 = *arg1;
                    sp660 = -sp650;
                    sp664 = sp654;
                    sp668 = -temp_f0_7;
                    sp3C0 = Q(sp660);
                    temp_v0_10 = *arg1;
                    temp_f21_6 = M2C_FIELD((temp_v1_6 + 0x70), f32 (**)(char *, f32), 4)((char *)arg1 + M2C_FIELD(temp_v1_6, s16 *, 0x70), fparg0);
                    temp_v0_11 = *arg1;
                    temp_f20_6 = M2C_FIELD((temp_v0_10 + 0x78), f32 (**)(char *, f32), 4)((char *)arg1 + M2C_FIELD(temp_v0_10, s16 *, 0x78), fparg0);
                    temp_f0_8 = M2C_FIELD((temp_v0_11 + 0x80), f32 (**)(char *, f32), 4)((char *)arg1 + M2C_FIELD(temp_v0_11, s16 *, 0x80), fparg0);
                    sp650 = temp_f21_6;
                    sp654 = temp_f20_6;
                    sp658 = temp_f0_8;
                    sp660 = -sp650;
                    sp664 = sp654;
                    sp668 = -temp_f0_8;
                    func_00415240(&sp370, &sp660);
                    temp_v0_12 = *arg1;
                    temp_v0_13 = *arg1;
                    temp_f21_7 = M2C_FIELD((temp_v0_12 + 0x90), f32 (**)(char *, f32), 4)((char *)arg1 + M2C_FIELD(temp_v0_12, s16 *, 0x90), fparg0);
                    temp_v0_14 = *arg1;
                    temp_f20_7 = M2C_FIELD((temp_v0_13 + 0x98), f32 (**)(char *, f32), 4)((char *)arg1 + M2C_FIELD(temp_v0_13, s16 *, 0x98), fparg0);
                    temp_f0_9 = M2C_FIELD((temp_v0_14 + 0xA0), f32 (**)(char *, f32), 4)((char *)arg1 + M2C_FIELD(temp_v0_14, s16 *, 0xA0), fparg0);
                    sp650 = temp_f21_7;
                    sp654 = temp_f20_7;
                    sp658 = temp_f0_9;
                    temp_v1_7 = *arg1;
                    sp660 = -sp650;
                    sp664 = sp654;
                    sp668 = -temp_f0_9;
                    sp3D0 = Q(sp660);
                    temp_v0_15 = *arg1;
                    temp_f21_8 = M2C_FIELD((temp_v1_7 + 0xA8), f32 (**)(char *, f32), 4)((char *)arg1 + M2C_FIELD(temp_v1_7, s16 *, 0xA8), fparg0);
                    temp_v0_16 = *arg1;
                    temp_f20_8 = M2C_FIELD((temp_v0_15 + 0xB0), f32 (**)(char *, f32), 4)((char *)arg1 + M2C_FIELD(temp_v0_15, s16 *, 0xB0), fparg0);
                    temp_f0_10 = M2C_FIELD((temp_v0_16 + 0xB8), f32 (**)(char *, f32), 4)((char *)arg1 + M2C_FIELD(temp_v0_16, s16 *, 0xB8), fparg0);
                    sp650 = temp_f21_8;
                    sp654 = temp_f20_8;
                    sp658 = temp_f0_10;
                    sp660 = -sp650;
                    sp664 = sp654;
                    sp668 = -temp_f0_10;
                    sp3E0 = Q(sp660);
                    if (temp_s7_2 == 3) {
                        sp650 = 0x1.06a7ee0000000p-1f;
                        sp654 = 0x1.05a1ca0000000p+0f;
                        sp658 = -0x1.ae147a0000000p-4f;
                        sp660 = -sp650;
                        sp664 = sp654;
                        sp668 = -(-0x1.ae147a0000000p-4f);
                    } else {
                        temp_v0_17 = *arg1;
                        temp_v0_18 = *arg1;
                        temp_f21_9 = M2C_FIELD((temp_v0_17 + 0xA8), f32 (**)(char *, f32), 4)((char *)arg1 + M2C_FIELD(temp_v0_17, s16 *, 0xA8), fparg0);
                        temp_v0_19 = *arg1;
                        temp_f20_9 = M2C_FIELD((temp_v0_18 + 0xB0), f32 (**)(char *, f32), 4)((char *)arg1 + M2C_FIELD(temp_v0_18, s16 *, 0xB0), fparg0);
                        temp_f0_11 = M2C_FIELD((temp_v0_19 + 0xB8), f32 (**)(char *, f32), 4)((char *)arg1 + M2C_FIELD(temp_v0_19, s16 *, 0xB8), fparg0);
                        sp650 = temp_f21_9;
                        sp654 = temp_f20_9;
                        sp658 = temp_f0_11;
                        sp660 = -sp650;
                        sp664 = sp654;
                        sp668 = -temp_f0_11;
                    }
                    sp3F0 = Q(sp660);
                    temp_v0_20 = *arg1;
                    temp_a1_2 = M2C_FIELD((temp_v0_20 + 0x88), f32 (**)(void *), 4);
                    temp_v1_8 = *arg1;
                    sp450 = temp_a1_2((char *)arg1 + M2C_FIELD(temp_v0_20, s16 *, 0x88));
                    if (M2C_FIELD((temp_v1_8 + 0x170), s32 (**)(char *), 4)((char *)arg1 + M2C_FIELD(temp_v1_8, s16 *, 0x170)) != 0) {
                        temp_v0_21 = *arg1;
                        temp_v0_22 = *arg1;
                        temp_f21_10 = M2C_FIELD((temp_v0_21 + 0x110), f32 (**)(char *, f32), 4)((char *)arg1 + M2C_FIELD(temp_v0_21, s16 *, 0x110), fparg0);
                        temp_v0_23 = *arg1;
                        temp_f20_10 = M2C_FIELD((temp_v0_22 + 0x118), f32 (**)(char *, f32), 4)((char *)arg1 + M2C_FIELD(temp_v0_22, s16 *, 0x118), fparg0);
                        temp_f0_12 = M2C_FIELD((temp_v0_23 + 0x120), f32 (**)(char *, f32), 4)((char *)arg1 + M2C_FIELD(temp_v0_23, s16 *, 0x120), fparg0);
                        sp650 = temp_f21_10;
                        sp654 = temp_f20_10;
                        sp658 = temp_f0_12;
                        temp_v1_9 = *arg1;
                        sp660 = -sp650;
                        sp664 = sp654;
                        sp668 = -sp658;
                        sp400 = Q(sp660);
                        temp_v0_24 = *arg1;
                        temp_f21_11 = M2C_FIELD((temp_v1_9 + 0x128), f32 (**)(char *, f32), 4)((char *)arg1 + M2C_FIELD(temp_v1_9, s16 *, 0x128), fparg0);
                        temp_v0_25 = *arg1;
                        temp_f20_11 = M2C_FIELD((temp_v0_24 + 0x130), f32 (**)(char *, f32), 4)((char *)arg1 + M2C_FIELD(temp_v0_24, s16 *, 0x130), fparg0);
                        temp_f0_13 = M2C_FIELD((temp_v0_25 + 0x138), f32 (**)(char *, f32), 4)((char *)arg1 + M2C_FIELD(temp_v0_25, s16 *, 0x138), fparg0);
                        sp650 = temp_f21_11;
                        sp654 = temp_f20_11;
                        sp658 = temp_f0_13;
                        temp_v1_10 = *arg1;
                        sp660 = -sp650;
                        sp664 = sp654;
                        sp668 = -temp_f0_13;
                        sp410 = Q(sp660);
                        temp_v0_26 = *arg1;
                        temp_f21_12 = M2C_FIELD((temp_v1_10 + 0x140), f32 (**)(char *, f32), 4)((char *)arg1 + M2C_FIELD(temp_v1_10, s16 *, 0x140), fparg0);
                        temp_v0_27 = *arg1;
                        temp_f20_12 = M2C_FIELD((temp_v0_26 + 0x148), f32 (**)(char *, f32), 4)((char *)arg1 + M2C_FIELD(temp_v0_26, s16 *, 0x148), fparg0);
                        temp_f0_14 = M2C_FIELD((temp_v0_27 + 0x150), f32 (**)(char *, f32), 4)((char *)arg1 + M2C_FIELD(temp_v0_27, s16 *, 0x150), fparg0);
                        sp650 = temp_f21_12;
                        sp654 = temp_f20_12;
                        sp658 = temp_f0_14;
                        sp660 = -sp650;
                        sp664 = sp654;
                        sp668 = -temp_f0_14;
                        func_004152B0(&sp370, 0, &sp660);
                        temp_v0_28 = *arg1;
                        temp_v0_29 = *arg1;
                        temp_f21_13 = M2C_FIELD((temp_v0_28 + 0x158), f32 (**)(char *, f32), 4)((char *)arg1 + M2C_FIELD(temp_v0_28, s16 *, 0x158), fparg0);
                        temp_v0_30 = *arg1;
                        temp_f20_13 = M2C_FIELD((temp_v0_29 + 0x160), f32 (**)(char *, f32), 4)((char *)arg1 + M2C_FIELD(temp_v0_29, s16 *, 0x160), fparg0);
                        temp_f0_15 = M2C_FIELD((temp_v0_30 + 0x168), f32 (**)(char *, f32), 4)((char *)arg1 + M2C_FIELD(temp_v0_30, s16 *, 0x168), fparg0);
                        sp650 = temp_f21_13;
                        sp654 = temp_f20_13;
                        sp658 = temp_f0_15;
                        sp660 = -sp650;
                        sp664 = sp654;
                        sp668 = -temp_f0_15;
                        func_004152B0(&sp370, 1, &sp660);
                        sp490 = 0;
                    } else {
                        temp_v0_31 = *arg1;
                        temp_v0_32 = *arg1;
                        temp_f20_14 = M2C_FIELD((temp_v0_31 + 0x58), f32 (**)(char *, f32), 4)((char *)arg1 + M2C_FIELD(temp_v0_31, s16 *, 0x58), fparg0);
                        if (temp_f20_14 < M2C_FIELD((temp_v0_32 + 0x90), f32 (**)(char *, f32), 4)((char *)arg1 + M2C_FIELD(temp_v0_32, s16 *, 0x90), fparg0)) {
                            sp490 = 2;
                            sp264 = func_003859D8(&sp5C0);
                            sp26C = func_00385A48(&sp5C0);
                            sp274 = func_00385A98(&sp5C0);
                            sp72C = 1;
                        } else {
                            sp490 = 1;
                            sp260 = func_003859C8(&sp5C0);
                            sp268 = func_00385A38(&sp5C0);
                            sp270 = func_00385A88(&sp5C0);
                            sp72C = 0;
                        }
                    }
                    temp_v0_33 = *arg1;
                    sp730 = sp72C == 0;
                    sp498 = 0x1.921fb40000000p+0f;
                    sp494 = M2C_FIELD((temp_v0_33 + 0xE0), f32 (**)(char *, f32), 4)((char *)arg1 + M2C_FIELD(temp_v0_33, s16 *, 0xE0), fparg0);
                    var_f27 = 0x1.0000000000000p+0f;
                    sp630 = M2C_FIELD(arg0, f32 *, 0x7D0);
                    temp_f1_2 = M2C_FIELD(arg0, f32 *, 0x7D8);
                    sp660 = M2C_FIELD(arg0, f32 *, 0x7D4);
                    sp670 = temp_f1_2;
                    func_00488528(&sp650, sp630, sp660, temp_f1_2);
                    sp370 = M2C_FIELD(arg0, f32 *, 0x7C4);
                    sp374 = M2C_FIELD(arg0, f32 *, 0x7C8);
                    sp378 = M2C_FIELD(arg0, f32 *, 0x7CC);
                    sp680 = 0x0.0p+0f;
                    sp684 = 0x1.0000000000000p+0f;
                    sp688 = 0x0.0p+0f;
                    sp68C = 0x0.0p+0f;
                    func_0048D530(&sp690, &sp650, &sp680);
                    temp_v0_34 = sp690;
                    Q(sp660) = temp_v0_34;
                    sp380 = temp_v0_34;
                    sp4A0 = func_00385AA8(&sp5C0);
                    sp360 = func_00385AA8(&sp5C0);
                    sp22C = M2C_FIELD(sp714, f32 *, 4);
                    sp230 = func_003857A8(&sp5C0);
                    sp234 = func_003857C0(&sp5C0);
                    sp238 = func_003857E8(&sp5C0);
                    sp23C = func_00385800(&sp5C0);
                    temp_f0_16 = func_00385828(&sp5C0);
                    sp644 = temp_f0_16;
                    sp240 = ee_min(ee_max(temp_f0_16, 0x0.0p+0f), 0x1.0000000000000p+0f);
                    temp_f0_17 = func_00385840(&sp5C0);
                    sp644 = temp_f0_17;
                    sp244 = ee_min(ee_max(temp_f0_17, 0x0.0p+0f), 0x1.0000000000000p+0f);
                    if (temp_s7_2 == 5) {
                        temp_f20_15 = func_003858D8(&sp5C0);
                        sp248 = temp_f20_15 * func_00385A58(&sp5C0);
                        temp_f20_16 = func_003858E8(&sp5C0);
                        sp24C = temp_f20_16 * func_00385A58(&sp5C0);
                        temp_f20_17 = func_003858F8(&sp5C0);
                        sp250 = temp_f20_17 * func_00385A58(&sp5C0);
                        temp_f20_18 = func_00385908(&sp5C0);
                        sp254 = temp_f20_18 * func_00385A58(&sp5C0);
                    } else {
                        sp248 = func_003858D8(&sp5C0);
                        sp24C = func_003858E8(&sp5C0);
                        sp250 = func_003858F8(&sp5C0);
                        sp254 = func_00385908(&sp5C0);
                    }
                    sp258 = func_003859A8(&sp5C0);
                    sp25C = func_003859B8(&sp5C0);
                    temp_f0_18 = func_00385AC0(&sp5C0);
                    sp644 = temp_f0_18;
                    sp18 = ee_min(ee_max(temp_f0_18, 0x0.0p+0f), 0x1.0000000000000p+0f);
                    temp_f0_19 = func_00385AD0(&sp5C0);
                    sp644 = temp_f0_19;
                    temp_f0_20 = ee_min(ee_max(temp_f0_19, 0x0.0p+0f), 0x1.0000000000000p+0f);
                    sp2F0 = ((temp_s7_2 ^ 5) == 0) ? 1 : sp2F0;
                    sp1C = temp_f0_20;
                    temp_f20_19 = func_003856A8(&sp5C0);
                    if (temp_s7_2 != 1) {
                        temp_f1_3 = sp494;
                        temp_f0_21 = var_f24 + func_003856B8(&sp5C0);
                        sp630 = temp_f1_3;
                        var_f0 = temp_f1_3 + temp_f0_21;
                        sp644 = var_f0;
                        if (sp2E8 == 0) {
                            var_f0 = ee_min(ee_max(var_f0, sp280), sp27C);
                        }
                        sp278 = var_f0;
                        sp210 = 1;
                        temp_f1_4 = -0x1.921fb40000000p-1f;
                        sp640 = 0x1.921fb40000000p-1f;
                        temp_f0_22 = ee_min(ee_max(temp_f20_19, temp_f1_4), sp640);
                        sp6A0 = temp_f20_19;
                        sp644 = temp_f1_4;
                        sp6C = temp_f0_22;
                        temp_f0_23 = func_00385938(&sp5C0);
                        var_f3 = -0x1.e28c6e0000000p-1f;
                        sp644 = 0x1.e28c6e0000000p-1f;
                        var_f1 = (temp_f20_19 * 0x1.9999980000000p-3f) + temp_f0_23;
                        sp6A0 = var_f3;
                    } else {
                        temp_f2 = sp494;
                        temp_f0_24 = func_003856B8(&sp5C0) * 0x1.9999980000000p-4f;
                        sp630 = temp_f2;
                        var_f1_2 = temp_f2 + temp_f0_24;
                        sp6A0 = var_f1_2;
                        if (sp2E8 == 0) {
                            var_f1_2 = ee_min(ee_max(var_f1_2, sp280), sp27C);
                        }
                        sp278 = var_f1_2;
                        sp210 = 1;
                        temp_f1_5 = -0x1.921fb40000000p-1f;
                        sp644 = 0x1.921fb40000000p-1f;
                        temp_f0_25 = ee_min(ee_max(temp_f20_19, temp_f1_5), sp644);
                        sp640 = temp_f20_19;
                        sp6A0 = temp_f1_5;
                        sp6C = temp_f0_25;
                        temp_f0_26 = func_00385938(&sp5C0);
                        var_f3 = -0x1.e28c6e0000000p-1f;
                        sp644 = 0x1.e28c6e0000000p-1f;
                        sp6A0 = var_f3;
                        var_f1 = ((temp_f20_19 * 0x1.9999980000000p-3f) + temp_f0_26) * 0x1.9999980000000p-4f;
                    }
                    sp640 = var_f1;
                    sp60 = ee_min(ee_max(var_f1, var_f3), 0x1.e28c6e0000000p-1f);
                    var_f20_2 = 0x0.0p+0f;
                    temp_f0_27 = func_00385998(&sp5C0);
                    sp640 = temp_f0_27;
                    temp_f1_6 = -0x1.41b2f40000000p-2f;
                    temp_f0_28 = ee_min(ee_max(temp_f0_27, temp_f1_6), 0x1.41b2f40000000p-2f);
                    sp644 = 0x1.41b2f40000000p-2f;
                    sp64 = temp_f0_28;
                    sp6A0 = temp_f1_6;
                    sp2E0 = func_003859E8(&sp5C0);
                    sp84 = func_003859F8(&sp5C0);
                    temp_f21_14 = func_00385700(&sp5C0);
                    temp_f25 = func_00385A08(&sp5C0);
                    temp_f26 = func_00385A58(&sp5C0);
                    temp_f0_29 = func_00385AA8(&sp5C0);
                    if (temp_f0_29 <= 0x1.0000000000000p+0f) {
                        var_f1_3 = temp_f0_29 * temp_f25;
                        var_f0_2 = (0x1.0000000000000p+0f - temp_f0_29) * temp_f21_14;
                        goto block_78;
                    }
                    if (temp_f0_29 <= 0x1.0000000000000p+1f) {
                        temp_f1_7 = temp_f0_29 - 0x1.0000000000000p+0f;
                        var_f1_3 = temp_f1_7 * temp_f26;
                        var_f0_2 = (0x1.0000000000000p+0f - temp_f1_7) * temp_f25;
                        goto block_78;
                    }
                    if (temp_f0_29 <= 0x1.8000000000000p+1f) {
                        temp_f1_8 = temp_f0_29 - 0x1.0000000000000p+1f;
                        var_f1_3 = temp_f1_8 * temp_f21_14;
                        var_f0_2 = (0x1.0000000000000p+0f - temp_f1_8) * temp_f26;
block_78:
                        var_f20_2 = var_f0_2 + var_f1_3;
                    }
                    sp660 = 0x0.0p+0f;
                    sp664 = 0x0.0p+0f;
                    sp668 = 0x0.0p+0f;
                    sp670 = 0x0.0p+0f;
                    sp674 = 0x0.0p+0f;
                    sp678 = 0x0.0p+0f;
                    sp680 = 0x0.0p+0f;
                    sp688 = 0x0.0p+0f;
                    sp684 = 0x0.0p+0f;
                    if ((sp710 != 0) && (temp_v1_11 = *arg1, (M2C_FIELD((temp_v1_11 + 0x170), s32 (**)(char *), 4)((char *)arg1 + M2C_FIELD(temp_v1_11, s16 *, 0x170)) != 1))) {
                        if (sp72C == 0) {
                            var_f0_3 = func_00385778(&sp5C0);
                        } else {
                            var_f0_3 = func_00385788(&sp5C0);
                        }
                        var_f23_2 = var_f0_3;
                        if (sp730 == 0) {
                            var_f0_4 = func_00385778(&sp5C0);
                        } else {
                            var_f0_4 = func_00385788(&sp5C0);
                        }
                        var_f24_2 = var_f0_4;
                        if (sp720 != 0) {
                            if (temp_s7_2 == 5) {
                                func_003B1FA8(sp714, arg1, &sp6A0, &sp644, &sp640);
                            } else {
                                func_003B21D8(sp714, arg1, &sp6A0, &sp644, &sp640);
                            }
                            sp660 = sp6A0;
                            sp664 = sp644;
                            sp668 = sp640;
                            if (temp_s7_2 == 3) {
                                func_003B2370(arg1, &sp670, &sp674, &sp678, func_00385A18(&sp5C0));
                                func_003B2440(&sp680, &sp684, &sp688, func_00385A68(&sp5C0));
                            }
                            if (var_f20_2 < 0x1.0000000000000p-1f) {
                                temp_f5 = 0x1.0000000000000p+1f * var_f20_2;
                                sp630 = 0x1.ccccce0000000p-1f;
                                sp634 = 0x1.3333320000000p-2f;
                                temp_f0_30 = ee_min(ee_max((0x1.ccccce0000000p-1f * var_f20_2) + 0x1.3333320000000p-2f, 0x1.3333320000000p-2f), 0x1.8000000000000p-1f);
                                sp6A8 = 0x1.8000000000000p-1f;
                                sp6A4 = 0x1.3333320000000p-2f;
                                var_f23_2 = (temp_f0_30 * temp_f5) + (var_f23_2 * (0x1.0000000000000p+0f - temp_f5));
                            } else {
                                sp630 = -0x1.ccccce0000000p-1f;
                                temp_f2_2 = 0x1.0000000000000p+1f * (0x1.0000000000000p+0f - var_f20_2);
                                sp6A4 = 0x1.8000000000000p-1f;
                                sp6A8 = 0x1.3333320000000p-2f;
                                temp_f0_31 = ee_min(ee_max((-0x1.ccccce0000000p-1f * var_f20_2) + 0x1.3333320000000p+0f, 0x1.3333320000000p-2f), sp6A4);
                                sp634 = 0x1.3333320000000p+0f;
                                var_f23_2 = (temp_f0_31 * temp_f2_2) + (var_f23_2 * (0x1.0000000000000p+0f - temp_f2_2));
                            }
                        }
                    } else {
                        var_f24_2 = 0x1.9999980000000p-3f;
                        var_f23_2 = 0x1.9999980000000p-3f;
                    }
                    sp220 = ee_min(ee_max(temp_f21_14, 0x0.0p+0f), 0x1.0000000000000p+0f);
                    temp_f1_9 = ee_min(ee_max(temp_f25, 0x0.0p+0f), 0x1.0000000000000p+0f);
                    temp_f0_32 = ee_min(ee_max(temp_f26, 0x0.0p+0f), 0x1.0000000000000p+0f);
                    sp6A8 = temp_f21_14;
                    sp6A8 = temp_f25;
                    sp224 = temp_f1_9;
                    sp6A8 = temp_f26;
                    sp228 = temp_f0_32;
                    sp310 = Q(sp670);
                    sp300 = Q(sp660);
                    sp320 = Q(sp680);
                    temp_f21_15 = func_00385738(&sp5C0);
                    sp334 = func_00385748(&sp5C0);
                    temp_f0_33 = ee_min(ee_max(var_f23_2, 0x0.0p+0f), 0x1.0000000000000p+0f);
                    sp6A8 = var_f23_2;
                    sp330 = temp_f21_15;
                    ((float *)stack)[sp72C] = temp_f0_33;
                    temp_f0_34 = ee_min(ee_max(var_f24_2, 0x0.0p+0f), 0x1.0000000000000p+0f);
                    sp6A8 = var_f24_2;
                    ((float *)stack)[sp730] = temp_f0_34;
                    sp6B0 = func_00385878(&sp5C0);
                    sp6B4 = func_00385888(&sp5C0);
                    temp_f0_35 = func_00385898(&sp5C0);
                    sp6B8 = temp_f0_35;
                    sp6E0 = sp6B0;
                    sp6E4 = sp6B4;
                    sp6E8 = temp_f0_35;
                    sp6C0 = 1;
                    sp6D0 = Q(sp6E0);
                    sp6E0 = 0x0.0p+0f;
                    sp6E4 = 0x0.0p+0f;
                    sp6E8 = 0x0.0p+0f;
                    sp6F0 = 0x0.0p+0f;
                    sp6F4 = 0x0.0p+0f;
                    sp6F8 = 0x0.0p+0f;
                    if (sp724 != 0) {
                        if (temp_s7_2 != 1) {
                            if ((u32) (temp_s7_2 - 3) >= 2U) {
                                temp_v1_12 = *arg1;
                                if (M2C_FIELD((temp_v1_12 + 0x178), s32 (**)(char *), 4)((char *)arg1 + M2C_FIELD(temp_v1_12, s16 *, 0x178)) != 1) {
                                    temp_f20_20 = func_003856D8(&sp5C0);
                                    temp_f20_21 = ee_max(temp_f20_20, func_003856E8(&sp5C0));
                                    sp6F4 = func_00385758(&sp5C0) * -0x1.47ae140000000p-4f;
                                    sp6E4 = temp_f20_21 * -0x1.47ae140000000p-4f;
                                }
                            }
                        } else {
                            var_f27 = 0x1.0000000000000p-2f;
                            sp6F4 = M2C_FIELD(sp714, f32 *, 0x58) * 0x1.f3b6440000000p-5f;
                            sp6E4 = M2C_FIELD(sp714, f32 *, 0x5C) * 0x1.f3b6440000000p-5f;
                            sp6F8 = M2C_FIELD(sp714, f32 *, 0x58) * -0x1.de353e0000000p-2f;
                            sp6E8 = M2C_FIELD(sp714, f32 *, 0x5C) * -0x1.de353e0000000p-2f;
                            temp_f0_36 = func_004860E8(0x6A2478, 0xA, func_003856D8(&sp5C0)) * 0x1.9999980000000p-4f;
                            sp700 = 0x0.0p+0f;
                            sp708 = 0x0.0p+0f;
                            sp704 = temp_f0_36;
                            sp350 = Q(sp700);
                        }
                    }
                    sp290 = Q(sp6F0);
                    sp2A0 = Q(sp6E0);
                    sp2B8 = func_00385858(&sp5C0);
                    sp2BC = func_00385868(&sp5C0);
                    temp_f0_37 = func_003858A8(&sp5C0) * var_f27;
                    sp6A8 = 0x0.0p+0f;
                    sp6A4 = 0x1.0000000000000p+0f;
                    sp6A0 = temp_f0_37;
                    sp78 = ee_min(ee_max(temp_f0_37, sp6A8), sp6A4);
                    temp_f20_22 = func_00385698(&sp5C0);
                    temp_f21_16 = func_003858C8(&sp5C0);
                    sp74 = (temp_f20_22 - temp_f21_16) + (func_003858B8(&sp5C0) * 0x1.9999980000000p-4f) + 0x1.6571820000000p-4f;
                    temp_f20_23 = func_00385688(&sp5C0);
                    sp7C = temp_f20_23 + (func_003858B8(&sp5C0) * 0x1.0000000000000p-2f);
                    func_0057D8A0(&sp6A8, &sp6A4, -func_003858B8(&sp5C0));
                    func_00421DF8(&sp700, &sp650, 0, sp6A4, sp6A8);
                    spA0 = sp700;
                    spA4 = sp704;
                    spA8 = sp708;
                    if (temp_s7_2 == 1) {
                        func_00416FD8((float *)stack, sp71C, &sp370, NULL);
                    } else {
                        func_00416FD8((float *)stack, sp71C, &sp370, &sp6C0);
                    }
                    M2C_FIELD(arg0, f32 *, 0x690) = func_00385918(&sp5C0);
                    M2C_FIELD(arg0, f32 *, 0x694) = func_00385928(&sp5C0);
                    M2C_FIELD(sp71C, f32 *, 0x688) = func_00385948(&sp5C0);
                    var_v0 = sp728;
                    M2C_FIELD(sp71C, f32 *, 0x68C) = func_00385958(&sp5C0);
                    /* Duplicate return node #0x1.c400000000000p+6 Try simplifying control flow for better match */
                    return var_v0;
                }
            } else {
                return var_v0;
            }
        } else {
            goto block_4;
        }
    } else {
block_4:
        M2C_FIELD(arg0, s32 *, 0x6BC) = 0;
        return 0;
    }
}
