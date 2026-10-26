/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native correction of the provisional draft drafts/0036d818/0036d818_e6d7e1402a4bf0ad.cpp (argument order, argcheck.py orders): func_00356E58 is (int, float) as defined; the draft put the float first, the same EE call but other stack slots natively. */
#include "types.h"
#include "m2c_macros.h"
struct FloatArray { char pad[0x520]; float value[2]; };
struct ByteArray { char pad[0x20]; u8 value[8]; };
struct Info { u8 unknown, wheels, axles; char pad[9]; u8 wheelMap[4]; u8 axleMap[4]; char pad14[0x28]; s8 direction[8]; };
static inline int front(Info *p, int i) { int result = 0; if (i < p->wheels) result = p->direction[i] > 0; return result; }
static inline int rear(Info *p, int i) { int result = 0; if (i < p->wheels) result = p->direction[i] < 0; return result; }
static inline u8 axle(Info *p, int i) { u8 result = 0; if (i < p->axles) result = p->axleMap[i]; return result; }

extern "C" void func_00354CE8(char *);                      /* extern */
extern "C" f32 func_003554A0(char *, f32);                     /* extern */
extern "C" f32 func_003560F0(char *);                          /* extern */
extern "C" f32 func_00356BB8(char *);                          /* extern */
extern "C" f32 func_00356E58(s32, f32);                        /* extern */
extern "C" char *func_00359510(char *);                        /* extern */
extern "C" s32 func_00368398(char *) throw();                          /* extern */
extern "C" f32 func_00368628(char *) throw();                          /* extern */
extern "C" f32 func_0036D548(char *, f32, f32, f32) throw();           /* extern */
extern "C" f32 func_0036D738(f32) throw() __attribute__((const));                             /* extern */
extern "C" s32 func_0036D7A8(char *) throw();                          /* extern */
extern "C" s32 func_003F1F88(char *);                          /* extern */
extern "C" s32 func_003F1F98(char *);                          /* extern */
extern "C" f32 func_003F2110(char *);                          /* extern */

extern "C" void func_0036D818(char *arg0) {
    f32 sp[4];
    f32 inertia[2];
    f32 *temp_a1_2;
    f32 *temp_v0_4;
    f32 *temp_v1_3;
    f32 *var_a1;
    f32 temp_f0;
    f32 temp_f0_2;
    f32 temp_f1;
    f32 temp_f1_2;
    f32 temp_f20;
    f32 temp_f24;
    f32 temp_f2;
    f32 temp_f2_2;
    f32 temp_f3;
    f32 temp_f4_2;
    f32 var_f0;
    f32 var_f0_2;
    f32 var_f0_3;
    f32 var_f0_4;
    f32 var_f1;
    f32 var_f1_2;
    f32 var_f20;
    f32 var_f21;
    f32 var_f23;
    f32 var_f2;
    f32 var_f3;
    f32 var_f4;
    f32 var_f5;
    f32 *var_v1_2;
    s32 temp_a0_3;
    s32 temp_v1_4;
    s32 var_a2_2;
    s32 var_a2_3;
    s32 var_a3;
    s32 var_a3_2;
    s32 var_a3_3;
    s32 var_a4;
    s32 var_s6;
    u32 var_a2;
    u8 *var_a0;
    u8 *var_a2_5;
    u8 *var_a4_2;
    s32 temp_a0_2;
    u8 temp_a0_5;
    u8 temp_a1;
    u8 temp_a2;
    u8 temp_s4;
    u8 temp_v0_2;
    u8 temp_v0_3;
    u8 temp_v0_5;
    u8 temp_v1;
    u8 temp_v1_2;
    u8 temp_v1_5;
    u8 temp_v1_6;
    u8 temp_v1_7;
    s32 var_a0_3;
    u8 var_a2_4;
    s32 var_s0;
    s32 var_s0_2;
    s32 var_v1;
    s32 var_v1_3;
    char *temp_a0;
    char *temp_a0_4;
    char *temp_s1;
    char *temp_s2;
    char *temp_s3;
    char *temp_v0;
    char *var_a0_2;

    temp_s1 = arg0 + 0x104;
    var_f23 = 0x0.0p+0f;
    temp_v0 = M2C_FIELD(arg0, char **, 0x10);
    temp_s2 = temp_v0 + 0x234;
    temp_s3 = func_00359510(temp_v0);
    temp_f24 = M2C_FIELD(arg0, f32 *, 0x54C);
    if (M2C_FIELD(temp_s1, u8 *, 0x522) != 0) {
        {
            for (var_a4 = 0; var_a4 < ((Info*)temp_s3)->wheels; var_a4++) {
                temp_a0 = arg0 + (var_a4 * 0xEC) + 0x164;
                var_a3 = front((Info*)temp_s3, var_a4);
                var_a2 = rear((Info*)temp_s3, var_a4);
                temp_v1 = M2C_FIELD(temp_s2, u8 *, 0);
                switch (temp_v1) {                  /* switch 1 */
                case 0:                             /* switch 1 */
                case 4:                             /* switch 1 */
                case 5:                             /* switch 1 */
                case 6:                             /* switch 1 */
                    if (var_a2 != 0) {
                        M2C_FIELD(temp_a0, u8 *, 0x46) = (u8) (M2C_FIELD(temp_a0, u8 *, 0x46) | 8);
                    }
                    break;
                case 1:                             /* switch 1 */
                case 9:                             /* switch 1 */
                    if (var_a3 != 0) {
                        M2C_FIELD(temp_a0, u8 *, 0x46) |= 8;
                    }
                    break;
                case 2:                             /* switch 1 */
                    if (var_a3 != 0) {
                        M2C_FIELD(temp_a0, u8 *, 0x46) = (u8) (M2C_FIELD(temp_a0, u8 *, 0x46) | 8);
                    }
                    if (var_a2 != 0) {
                        M2C_FIELD(temp_a0, u8 *, 0x46) = (u8) (M2C_FIELD(temp_a0, u8 *, 0x46) | 4);
                    }
                    break;
                case 3:                             /* switch 1 */
                case 7:                             /* switch 1 */
                case 8:                             /* switch 1 */
                case 10:                            /* switch 1 */
                    if (var_a3 != 0) {
                        M2C_FIELD(temp_a0, u8 *, 0x46) = (u8) (M2C_FIELD(temp_a0, u8 *, 0x46) | 4);
                    }
                    if (var_a2 != 0) {
                        M2C_FIELD(temp_a0, u8 *, 0x46) |= 8;
                    }
                    break;
                }
            }
        }
    }
    if (func_00368398(temp_s2) != 0) {
        var_f23 = func_00368628(arg0);
        if (M2C_FIELD(temp_s1, u8 *, 0x522) == 0) {
            var_f23 = 0x0.0p+0f;
        }
    } else {
        if (M2C_FIELD(temp_s1, u8 *, 0x522) != 0) {
            if (func_003F1F98(temp_s2) != 0) {
                var_f21 = func_003F2110(arg0);
            } else {
                if (M2C_FIELD(temp_s2, u8 *, 0) == 7) {
                    var_f21 = func_003554A0(arg0, func_003560F0(arg0));
                    var_f21 = var_f21 + func_00356BB8(arg0);
                } else {
                    if (M2C_FIELD(temp_s2, u8 *, 0x13) == 4) {
                    temp_f20 = M2C_FIELD(temp_s1, f32 *, 0x494);
                    var_f21 = func_003554A0(arg0, temp_f20);
                    var_f21 = var_f21 + func_00356E58(M2C_FIELD(temp_s1, s16 *, 0x4FA), temp_f20);
                    } else {
                        var_f21 = func_003554A0(arg0, M2C_FIELD(temp_s1, f32 *, 0x494));
                    }
                }
            }
            temp_v1_2 = M2C_FIELD(temp_s1, u8 *, 0x522);
            var_f23 = var_f21;
            if (temp_v1_2 != 1) {
                if (temp_v1_2 == 3) {
                    var_f23 = M2C_FIELD(temp_s2, f32 *, 0xB0);
                } else {
                    var_f23 = M2C_FIELD(temp_s1, f32 *, 0x4E8) * 0x1.9999980000000p-5f;
                    if (var_f21 > 0x0.0p+0f) var_f23 += func_0036D738(M2C_FIELD(temp_s1, f32 *, 0x50C) - M2C_FIELD(temp_s1, f32 *, 0x510)) * var_f21;
                }
                if (M2C_FIELD(temp_s1, f32 *, 0x50C) < M2C_FIELD(temp_s1, f32 *, 0x510)) var_f23 = -var_f23;
                M2C_FIELD(temp_s1, f32 *, 0x4FC) += (var_f21 - var_f23) * M2C_FIELD(temp_s2, f32 *, 0xC8) * temp_f24;
            }
        }
    }
    func_00354CE8(arg0);
    if (M2C_FIELD(temp_s2, u8 *, 0) != 7) {
        M2C_FIELD(temp_s1, f32 *, 0x51C) = var_f23;
        if (!(var_f23 >= 0x0.0p+0f)) {
            M2C_FIELD(temp_s1, f32 *, 0x51C) = (f32) -var_f23;
        }
    }
    var_s0 = M2C_FIELD(temp_s1, u8 *, 0x520);
    if (func_003F1F88(temp_s2) != 0) {
        var_s0 = ((s32) var_s0 >= 2) ? 1U : var_s0;
    }
    var_f20 = 0x0.0p+0f;
    var_a2_2 = 0;
    var_f23 = var_f23 * M2C_FIELD(((var_s0 * 4) + temp_s2), f32 *, 0x40);
    if (M2C_FIELD(temp_s3, u8 *, 2) != 0) {
        var_v1_2 = sp;
        do {
            *var_v1_2 = 0;
            var_a2_2 += 1;
            var_v1_2 += 1;
        } while (var_a2_2 < (s32) M2C_FIELD(temp_s3, u8 *, 2));
    }
    var_a2_3 = 0;
    if (M2C_FIELD(temp_s3, u8 *, 1) != 0) {
        var_a1 = (f32 *)(arg0 + 0x1E8);
        var_a0 = (u8 *)temp_s3 + 0x14;
        do {
            temp_v0_2 = *var_a0;
            var_a0 += 1;
            var_a2_3 += 1;
            temp_v1_3 = sp + temp_v0_2;
            *temp_v1_3 += *var_a1;
            var_a1 += 0xEC / 4;
        } while (var_a2_3 < (s32) M2C_FIELD(temp_s3, u8 *, 1));
    }
    if (M2C_FIELD(temp_s2, u8 *, 1) != 0) {
        temp_v0_3 = M2C_FIELD(temp_s2, u8 *, 3);
        temp_v1_4 = 1 - temp_v0_3;
        temp_v0_4 = sp + temp_v1_4;
        *temp_v0_4 *= M2C_FIELD((temp_v1_4 * 4 + temp_s2), f32 *, 0x90) * M2C_FIELD(((temp_v0_3 * 4) + temp_s2), f32 *, 0x98);
    }
    temp_a0_2 = M2C_FIELD(temp_s2, u8 *, 0);
    var_s6 = 0;
    switch (temp_a0_2) {
    case 0: case 1: case 7: break;                            /* switch 3 */
    case 2:                                         /* switch 3 */
        var_s6 = 1;
        break;
    case 3:                                         /* switch 3 */
    case 10:                                        /* switch 3 */
        var_s6 = 2;
        break;
    case 4:                                         /* switch 3 */
    case 5:                                         /* switch 3 */
    case 6:                                         /* switch 3 */
    case 8:                                         /* switch 3 */
    case 9:                                         /* switch 3 */
        var_f20 = 0x0.0p+0f;
        var_f3 = 0x0.0p+0f;
        switch (temp_a0_2) {
                        /* switch 4 */
        case 4:                                     /* switch 4 */
            temp_f2 = M2C_FIELD(temp_s2, f32 *, 4);
            if (temp_f2 != 0x1.0000000000000p+0f) {
                if (!(M2C_FIELD(temp_s1, f32 *, 0x490) > 0x1.9999980000000p-3f)) {
                    var_f3 = 0x1.7ecffe0000000p+16f;
                    if (!(var_f23 <= 0x0.0p+0f)) {
                        if ((M2C_FIELD(temp_s1, u32 *, 0x520) & 0xFF00FF) != 0x10000) {
                            var_f3 = temp_f2 * var_f23;
                        }
                    }
                }
            } else {
                var_f3 = 0x1.7ecffe0000000p+16f;
            }
            break;
        case 5:                                     /* switch 4 */
            var_f3 = func_0036D548(arg0, var_f23, sp[0], sp[1]);
            if ((var_f3 != 0x1.7ecffe0000000p+16f) && (var_f3 != -0x1.7ecffe0000000p+16f)) {
                var_f3 = var_f3 * var_f23 * 0x1.0000000000000p-1f;
            }
            break;
        case 6:                                     /* switch 4 */
            if (!(M2C_FIELD(temp_s1, f32 *, 0x490) > 0x1.9999980000000p-3f)) {
                var_f3 = 0x1.7ecffe0000000p+16f;
                if ((M2C_FIELD(temp_s1, u32 *, 0x520) & 0xFF00FF) != 0x10000) {
                    var_f2 = M2C_FIELD(temp_s1, f32 *, 0x468);
                    if (var_f2 < 0x0.0p+0f) {
                        var_f2 = -var_f2;
                    }
                    var_f2 = 0x1.0000000000000p+0f - (var_f2 * M2C_FIELD(temp_s2, f32 *, 4));
                    if (var_f2 < 0x0.0p+0f) {
                        var_f2 = 0x0.0p+0f;
                    }
                    if (var_f2 < 0x1.fae1460000000p-1f) {
                        var_f3 = var_f2;
                    }
                }
            }
            if ((var_f3 != 0x1.7ecffe0000000p+16f) && (var_f3 != -0x1.7ecffe0000000p+16f)) var_f3 = var_f3 * var_f23 * 0x1.0000000000000p-1f;
            break;
        case 8:                                     /* switch 4 */
        case 9:                                     /* switch 4 */
            var_f3 = M2C_FIELD(temp_s2, f32 *, 4) * var_f23;
            break;
        }
        var_a2_4 = M2C_FIELD(temp_s1, u8 *, 0x523);
        if (var_a2_4 != 0) {
            if (var_f3 == 0x1.7ecffep+16f) {
                var_s6 = 3;
            } else if (var_f3 == -0x1.7ecffep+16f) {
                M2C_FIELD(temp_s1, u8 *, 0x523) = 0;
                var_a2_4 = 0;
            } else {
                temp_v1_5 = M2C_FIELD(temp_s2, u8 *, 3);
                var_f0_2 = (sp[1-temp_v1_5] - sp[temp_v1_5]) - var_f23;
                if (var_f0_2 < 0.0f) var_f0_2 = -var_f0_2;
                var_f1 = var_f3;
                if (var_f3 < 0.0f) var_f1 = -var_f3;
                if (var_f1 < var_f0_2) {
                    M2C_FIELD(temp_s1, u8 *, 0x523) = 0;
                    var_a2_4 = 0;
                } else var_s6 = 3;
            }
        }
        if (var_a2_4 == 0) {
            var_f20 = 0x0.0p+0f;
            if (var_f3 != -0x1.7ecffe0000000p+16f) {
                if (var_f3 != 0x1.7ecffep+16f) {
                    var_f20 = var_f3;
                    if (var_f20 < 0.0f) var_f20 = -var_f20;
                } else {
                    var_f20 = M2C_FIELD(temp_s2, f32 *, 8);
                }
            }
            if (var_f20 != 0x0.0p+0f) {
                var_s6 = 1;
            }
        }
        if (var_s6 != 0) {
            var_a3_2 = 0;
            if (M2C_FIELD(temp_s3, u8 *, 1) != 0) {
                var_a0_2 = arg0 + 0x164;
                var_a2_5 = (u8 *)temp_s3 + 0x2C;
                do {
                    var_a3_2 += 1;
                    if (*var_a2_5 != M2C_FIELD(temp_s2, u8 *, 3)) {
                        M2C_FIELD(var_a0_2, u8 *, 0x46) = (u8) (M2C_FIELD(var_a0_2, u8 *, 0x46) | 4);
                    }
                    var_a0_2 += 0xEC;
                    var_a2_5 += 1;
                } while (var_a3_2 < (s32) M2C_FIELD(temp_s3, u8 *, 1));
            }
        }
        break;
    }
    var_f21 = 0x0.0p+0f;
    if (func_00368398(temp_s2) == 0) {
        temp_s4 = M2C_FIELD(temp_s1, u8 *, 0x522);
        if (temp_s4 != 1) {

        } else {
            var_s0_2 = M2C_FIELD(temp_s1, u8 *, 0x520);
            if (func_003F1F88(temp_s2) != 0) {
                var_s0_2 = ((s32) var_s0_2 >= 2) ? temp_s4 : var_s0_2;
            }
            var_f21 = M2C_FIELD(temp_s2, f32 *, 0xC) * M2C_FIELD(((var_s0_2 * 4) + temp_s2), f32 *, 0x40);
            if (M2C_FIELD(temp_s1, u8 *, 0x520) == 0) {
                var_f21 = -var_f21;
            }
            if (func_003F1F98(temp_s2) != 0) {
                var_f21 *= M2C_FIELD(temp_s1, f32 *, 0x518);
            }
        }
    }
    if (var_s6 == 3) {
        var_f20 = M2C_FIELD(temp_s1, f32 *, 0x52C);
        M2C_FIELD(temp_s1, f32 *, 0x52C) = (f32) (var_f20 + (((var_f23 + sp[0] + sp[1]) / (M2C_FIELD(temp_s2, f32 *, 0xBC) + M2C_FIELD(temp_s2, f32 *, 0xC0) + var_f21)) * temp_f24));
        s32 stop = func_0036D7A8(arg0);
        var_f3 = M2C_FIELD(temp_s1, f32 *, 0x52C);
        if ((stop != 0) && (((var_f3 * var_f20) < 0x0.0p+0f) || (var_f20 == 0x0.0p+0f))) {
            M2C_FIELD(temp_s1, f32 *, 0x52C) = 0x0.0p+0f;
            var_f3 = 0x0.0p+0f;
        }
        M2C_FIELD(temp_s1, f32 *, 0x530) = var_f3;
        temp_f0 = var_f23 * 0x1.0000000000000p-1f;
        M2C_FIELD(temp_s1, f32 *, 0x538) = temp_f0;
        M2C_FIELD(temp_s1, f32 *, 0x534) = temp_f0;
    } else if (var_s6 == 2) {
        var_f20 = (M2C_FIELD(temp_s1, f32 *, 0x52C) + M2C_FIELD(temp_s1, f32 *, 0x530)) * 0x1.0000000000000p-1f;
        float meanDelta = (((var_f23 + sp[0] + sp[1]) / (M2C_FIELD(temp_s2, f32 *, 0xBC) + M2C_FIELD(temp_s2, f32 *, 0xC0) + var_f21)) * temp_f24);
        var_f21 = var_f20;
        var_f20 += meanDelta;
        if ((func_0036D7A8(arg0) != 0) && (((var_f20 * var_f21) < 0x0.0p+0f) || (var_f21 == 0x0.0p+0f))) {
            var_f20 = 0x0.0p+0f;
        }
        var_f4 = M2C_FIELD(temp_s1, f32 *, 0x52C) - M2C_FIELD(temp_s1, f32 *, 0x530);
        float acceleration = ((sp[0] - sp[1]) - (M2C_FIELD(temp_s2, f32 *, 4) * var_f4)) * M2C_FIELD(temp_s2, f32 *, 0xC4) * temp_f24;
        var_f4 *= 0.5f;
        temp_f4_2 = var_f4;
        var_f4 += acceleration;
        if ((temp_f4_2 * var_f4) < 0x0.0p+0f) {
            var_f4 = 0x0.0p+0f;
        }
        temp_f0_2 = var_f23 * 0x1.0000000000000p-1f;
        float low = var_f20 - var_f4;
        float high = var_f20 + var_f4;
        M2C_FIELD(temp_s1, f32 *, 0x530) = low;
        M2C_FIELD(temp_s1, f32 *, 0x52C) = high;
        M2C_FIELD(temp_s1, f32 *, 0x534) = temp_f0_2;
        M2C_FIELD(temp_s1, f32 *, 0x538) = temp_f0_2;
    } else {
        temp_v1_6 = M2C_FIELD(temp_s2, u8 *, 0);
        var_f5 = 0x0.0p+0f;
        switch (temp_v1_6) {                        /* switch 6 */
        case 1:                                     /* switch 6 */
            M2C_FIELD(temp_s1, f32 *, 0x534) = var_f23;
            inertia[1] = 0x0.0p+0f;
            M2C_FIELD(temp_s1, f32 *, 0x538) = 0x0.0p+0f;
            inertia[0] = M2C_FIELD(temp_s2, f32 *, 0xBC) + var_f21;
            break;
        case 2:                                     /* switch 6 */
            inertia[0] = M2C_FIELD(temp_s2, f32 *, 0xBC) + var_f21;
            inertia[1] = M2C_FIELD(temp_s2, f32 *, 0xC0);
            var_f0_3 = M2C_FIELD(temp_s2, f32 *, 4) * (M2C_FIELD(temp_s1, f32 *, 0x52C) - M2C_FIELD(temp_s1, f32 *, 0x530));
            var_f1_2 = var_f23 - var_f0_3;
            M2C_FIELD(temp_s1, f32 *, 0x538) = var_f0_3;
            M2C_FIELD(temp_s1, f32 *, 0x534) = var_f1_2;
            break;
        case 0:                                     /* switch 6 */
        case 7:                                     /* switch 6 */
            inertia[0] = 0x0.0p+0f;
            M2C_FIELD(temp_s1, f32 *, 0x538) = var_f23;
            M2C_FIELD(temp_s1, f32 *, 0x534) = 0x0.0p+0f;
            inertia[1] = M2C_FIELD(temp_s2, f32 *, 0xC0) + var_f21;
            break;
        case 4:                                     /* switch 6 */
        case 5:                                     /* switch 6 */
        case 6:                                     /* switch 6 */
        case 8:                                     /* switch 6 */
            inertia[0] = M2C_FIELD(temp_s2, f32 *, 0xBC);
            inertia[1] = M2C_FIELD(temp_s2, f32 *, 0xC0) + var_f21;
            if (!(M2C_FIELD(temp_s1, f32 *, 0x52C) > M2C_FIELD(temp_s1, f32 *, 0x530))) {
                M2C_FIELD(temp_s1, f32 *, 0x534) = var_f20;
                M2C_FIELD(temp_s1, f32 *, 0x538) = (f32) (var_f23 - var_f20);
            } else {
                var_f0_3 = var_f23 + var_f20;
                var_f1_2 = -var_f20;
                M2C_FIELD(temp_s1, f32 *, 0x538) = var_f0_3;
                M2C_FIELD(temp_s1, f32 *, 0x534) = var_f1_2;
            }
            break;
        case 9:                                     /* switch 6 */
            inertia[0] = M2C_FIELD(temp_s2, f32 *, 0xBC) + var_f21;
            inertia[1] = M2C_FIELD(temp_s2, f32 *, 0xC0);
            if (M2C_FIELD(temp_s1, f32 *, 0x52C) > M2C_FIELD(temp_s1, f32 *, 0x530)) {
                M2C_FIELD(temp_s1, f32 *, 0x538) = var_f20;
                M2C_FIELD(temp_s1, f32 *, 0x534) = (f32) (var_f23 - var_f20);
            } else {
                M2C_FIELD(temp_s1, f32 *, 0x534) = (f32) (var_f23 + var_f20);
                M2C_FIELD(temp_s1, f32 *, 0x538) = (f32) -var_f20;
            }
            break;
        }
        if (var_s6 == 1) {
            var_f5 = M2C_FIELD(temp_s1, f32 *, 0x52C) - M2C_FIELD(temp_s1, f32 *, 0x530);
        }
        {
            for (var_a3_3 = 0; var_a3_3 < ((Info*)temp_s3)->axles; var_a3_3++) {
                temp_a2 = ((Info*)temp_s3)->wheelMap[var_a3_3];
                var_a0_3 = axle((Info*)temp_s3, var_a3_3);
                temp_a0_3 = var_a0_3 * 4;
                if (((Info*)temp_s2)->axleMap[var_a0_3] != 0) {
                    var_f0_4 = 0x0.0p+0f;
                    temp_f2_2 = (&inertia[0])[var_a0_3];
                    temp_a1_2 = &((FloatArray*)(temp_s1+0xc))->value[var_a0_3];
                    temp_f3 = *temp_a1_2;
                    if (temp_f2_2 != 0x0.0p+0f) {
                        var_f0_4 = ((((FloatArray*)(temp_s1+4))->value[var_a0_3+4] + *(sp + var_a0_3)) / temp_f2_2) * temp_f24;
                    }
                    temp_f1 = temp_f3 + var_f0_4;
                    *temp_a1_2 = temp_f1;
                    temp_v1_7 = ((ByteArray*)(temp_s3+4))->value[temp_a2];
                    temp_v1_7 = temp_v1_7 == 0x7f ? temp_a2 : temp_v1_7;
                    s32 paired = temp_v1_7;
                    if (((M2C_FIELD((arg0 + (temp_a2 * 0xEC)), f32 *, 0x1DC) != 0.0f) || (M2C_FIELD((arg0 + (paired * 0xEC)), f32 *, 0x1DC) != 0.0f)) && (((temp_f1 * temp_f3) < 0.0f) || (temp_f3 == 0.0f))) {
                        *temp_a1_2 = 0.0f;
                    }
                }
            }
        }
        if ((var_s6 == 1) && (((temp_f1_2 = M2C_FIELD(temp_s1, f32 *, 0x52C) - M2C_FIELD(temp_s1, f32 *, 0x530), (var_f5 > 0x0.0p+0f)) && (temp_f1_2 <= 0x0.0p+0f)) || ((var_f5 < 0x0.0p+0f) && (temp_f1_2 >= 0x0.0p+0f))) && ((temp_v0_5 = M2C_FIELD(temp_s2, u8 *, 3), temp_a0_4 = temp_s1 + 0xC, M2C_FIELD((((1 - temp_v0_5) * 4) + temp_a0_4), f32 *, 0x520) = (f32) M2C_FIELD(((temp_v0_5 * 4) + temp_a0_4), f32 *, 0x520), temp_a0_5 = M2C_FIELD(temp_s2, u8 *, 0), (((u32) ((temp_a0_5 + 0xFC) & 0xFF) < 3U) != 0)) || (temp_a0_5 == 9))) {
            M2C_FIELD(temp_s1, u8 *, 0x523) = 1U;
        }
    }
}
