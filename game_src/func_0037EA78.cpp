/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native correction of the provisional draft drafts/0037ea78/0037ea78_e40f2ab661720450.cpp (argument order, argcheck.py orders): func_00384EE8 takes three ints then two floats, func_00385058 a pointer then seven floats and func_003FC948 a pointer then three floats, as defined; the draft's orders are the same EE calls but other stack slots natively. */
typedef signed char s8; typedef unsigned char u8; typedef short s16; typedef int s32; typedef unsigned int u32; typedef long long s64; typedef float f32; typedef int M2C_UNK;
#define M2C_FIELD(expr, type_ptr, offset) (*(type_ptr)((char *)(expr) + (offset)))
static inline float sqrtf_(float x) { float r; __asm__("sqrt.s %0, %1" : "=f"(r) : "f"(x)); return r; }
static inline f32 *component(f32 *v, int i) { return v+i; }
extern "C" {
f32 func_0036EA50(f32 *) throw();
f32 func_0036EA98(f32 *) throw();
f32 func_0036EB98(f32, f32) throw();
f32 func_0036EBE8(f32, f32, f32) throw();
f32 func_0036F778(void *, f32) throw();
f32 func_0036F880(void *, f32) throw();
f32 func_0036F980(void *, f32) throw();
f32 func_0037E908(f32, f32) throw() __attribute__((const));
f32 func_0037E9A8(f32) throw() __attribute__((const));
f32 func_0037EA08(s32, f32 *, f32) throw();
void func_00384E58(f32 *, s32, f32, f32, f32);
void func_00384EE8(f32 *, s32, s32, f32, f32);
void func_00385058(f32 *, f32, f32, f32, f32, f32, f32, f32);
void func_00385098(f32 *, f32);
void func_00385190(f32 *, f32, f32, f32, f32, f32, f32);
void func_003851D0(f32 *, f32);
f32 func_00395400(s32);
f32 func_003955E8(s32, f32 *);
void func_003FC618(void *, f32 *);
void func_003FC658(void *, f32 *);
s32 func_003FC758(s32, const float *, f32);
f32 func_003FC948(s32 *, f32, f32, f32);
f32 func_003FD7E0(f32, f32, f32, f32);
void func_004A0690();
void func_004A06F0();
void func_004A53F8();
void func_004A5400();
void func_004A7454();
void func_004A7550(void *);
void func_004A7844(f32, f32, f32);
void func_004A7988(f32);
void func_004A79B0(f32);
void func_004A79D8(f32);
f32 func_00578598(s32);
f32 func_0057D008(f32);
void func_005A48D8(f32 *, M2C_UNK, M2C_UNK);
f32 func_005F5010(f32 *);
void func_005F5040(void *, void *);
void func_005F5268(f32 *, f32 *);

extern const float D_006212C0[];
}
static inline void set3(f32 *p, f32 x, f32 y, f32 z) { p[0]=x; p[1]=y; p[2]=z; }
static inline void setAngles(char *o, float x, float y, float z) {
    M2C_FIELD(o,f32 *,0x34)=x; M2C_FIELD(o,f32 *,0x38)=y; M2C_FIELD(o,f32 *,0x3C)=z;
}
static inline void setPosition(char *o, float x, float y, float z) {
    M2C_FIELD(o,f32 *,0x28)=x; M2C_FIELD(o,f32 *,0x2C)=y; M2C_FIELD(o,f32 *,0x30)=z;
}
static inline f32 fadeRatio(f32 value, f32 start, f32 end) {
    f32 d = end - start;
    f32 n = value - start;
    return 1.0f - n / d;
}
static inline f32 adjustAngles(f32 *pitch, f32 *yaw, f32 *off, f32 *sx, f32 *sy,
                              f32 *so, f32 *ssx, f32 *ssy, f32 fov, f32 half, f32 limit) {
    *off = *so;
    *sx = *ssx;
    *sy = *ssy;
    *yaw -= *off * fov * half;
    return func_0036EBE8(*pitch, fov, limit);
}
static inline void setPose(char *o, float x, float y, float z, float p, float q, float r) {
    M2C_FIELD(o,f32 *,0x34)=p; M2C_FIELD(o,f32 *,0x38)=q; M2C_FIELD(o,f32 *,0x3C)=r;
    M2C_FIELD(o,f32 *,0x28)=x; M2C_FIELD(o,f32 *,0x2C)=y; M2C_FIELD(o,f32 *,0x30)=z;
}
extern "C" s32 func_0037EA78(char *arg0, M2C_UNK arg1, M2C_UNK arg2, char *arg3) {
    f32 origin[4], target[4], delta[4], angle[4], offset[4], scale[4];
    s32 flag[4];
    f32 dist[4];
    struct CoursePos { float x,y,z; int id; } coursepos;
    f32 rear[4];
    f32 distance;

    f32 *temp_s1_2;
    f32 *var_s0_2;
    f32 *var_s0_3;
    f32 *var_s0_4;
    f32 *var_s1;
    f32 *var_s3;
    f32 *var_s4;
    f32 *var_s4_2;
    f32 temp_f0;
    f32 temp_f0_2;
    f32 temp_f0_3;
    f32 temp_f0_4;
    f32 temp_f0_5;
    f32 temp_f0_6;
    f32 temp_f0_7;
    f32 temp_f0_8;
    f32 temp_f12;
    f32 temp_f20;
    f32 temp_f20_2;
    f32 temp_f21;
    f32 temp_f22;
    f32 temp_f24;
    f32 temp_f2;
    f32 var_f1;
    f32 var_f20;
    f32 var_f21;
    f32 var_f22;
    f32 var_f23;
    f32 var_f25;
    f32 var_f26;
    s32 *var_s0;
    s32 temp_s0_2;
    s32 temp_s1;
    s32 temp_v0;
    s32 temp_v1;
    s32 var_v0;
    char *temp_s0;

    var_f22 = 0x0.0p+0f;
    temp_s0 = M2C_FIELD(arg0, char **, 0x194);
    var_f23 = 0x1.0000000000000p+0f;
    temp_s1 = M2C_FIELD(arg0, s32 *, 0x184);
    f32 zero = var_f22;
    f32 one = var_f23;
    var_f25 = 0x1.6800000000000p+6f;
    func_005A48D8(origin, 0, 0xC);
    if (temp_s0 != 0) {
        var_f22 = M2C_FIELD(temp_s0, f32 *, 0x10);
        var_f23 = M2C_FIELD(temp_s0, f32 *, 0x14);
        set3(origin, M2C_FIELD(temp_s0, f32 *, 4), M2C_FIELD(temp_s0, f32 *, 8), M2C_FIELD(temp_s0, f32 *, 0xC));
    }
    temp_f0 = func_003FD7E0(var_f22, var_f23, func_00395400(M2C_FIELD(arg0, s32 *, 0x18C)), M2C_FIELD(arg3, f32 *, 0x34));
    var_f26 = func_003FC948(&flag[0], var_f22, var_f23, temp_f0);
    temp_f24 = var_f26;
    temp_f2 = M2C_FIELD(arg0, f32 *, 0x198);
    if (temp_f2 > zero) {
        var_f26 = (f32) M2C_FIELD(arg0, s32 *, 0x140) / (temp_f2 * 60.0f);
        if (var_f26 <= zero) {
            var_f26 = zero;
        }
        if (var_f26 >= one) {
            var_f26 = one;
        }
    }
    f32 *ptarget = target;
    func_003FC618(arg3, ptarget);
    temp_f21 = M2C_FIELD(arg0, f32 *, 0x244);
    temp_f20 = M2C_FIELD(arg0, f32 *, 0x248) - temp_f21;
    f32 lift = (temp_f20 * func_0036F778(arg0 + 0x228, temp_f24)) + temp_f21;
    f32 *pty = component(target, 1);
    *pty += lift;
    f32 *pdelta = delta;
    func_005F5040(ptarget, pdelta);
    func_005F5268(pdelta, origin);
    distance = sqrtf_(func_005F5010(pdelta));
    f32 pitch = func_0036EA50(pdelta);
    f32 *pax = component(angle, 0);
    *pax = pitch;
    f32 yaw = func_0036EA98(pdelta);
    f32 *pay = component(angle, 1);
    *pay = yaw;
    if (M2C_FIELD(arg0, s32 *, 0x180) != 0) {
        temp_s0_2 = M2C_FIELD(arg0, s32 *, 0x188);
        coursepos.id = -1;
        coursepos.x = origin[0];
        coursepos.y = origin[1];
        coursepos.z = origin[2];
        var_f21 = func_003955E8(M2C_FIELD(arg0, s32 *, 0x18C), &coursepos.x);
        dist[0] = func_0037EA08(temp_s0_2, origin, var_f22);
        dist[1] = func_0037EA08(temp_s0_2, origin, var_f21);
        dist[2] = func_0037EA08(temp_s0_2, origin, var_f23);
        if (var_f21 <= var_f22) {
            var_f21 = var_f22;
        }
        if (var_f23 <= var_f21) {
            var_f21 = var_f23;
        }
        temp_f22 = (var_f21 - var_f22) / (var_f23 - var_f22);
        temp_f0_2 = func_0037E9A8(func_00578598(temp_s1));
        temp_f0_3 = 0x1.0000000000000p+1f * func_0057D008(0x1.0000000000000p+0f / (temp_f0_2 * dist[1]));
        temp_f20_2 = temp_f0_3 / 0x1.1df4680000000p-6f;
        M2C_FIELD(arg0, f32 *, 0x1F8) = temp_f0_2;
        M2C_FIELD(arg0, f32 *, 0x1EC) = var_f21;
        M2C_FIELD(arg0, f32 *, 0x1F0) = temp_f22;
        M2C_FIELD(arg0, f32 *, 0x1F4) = temp_f20_2;
        temp_v0 = func_003FC758(2, D_006212C0, func_00578598(temp_s1));
        M2C_FIELD(arg0, s32 *, 0x1C4) = temp_v0;
        switch (temp_v0) {
        case 0: {
            var_s0 = (s32 *)(arg0 + 0x1C8);
            temp_f0_5 = func_0037E908(func_00578598(temp_s1), dist[0]);
            M2C_FIELD(arg0, s32 *, 0x1CC) = 0;
            M2C_FIELD(arg0, f32 *, 0x1D0) = temp_f0_5;
            temp_f0_6 = func_0037E908(func_00578598(temp_s1), dist[2]);
            M2C_FIELD(arg0, f32 *, 0x1D4) = 1.0f;
            M2C_FIELD(arg0, f32 *, 0x1D8) = temp_f0_6;
            *var_s0 = 2;
            break;
        }
        case 1: {
            var_s0 = (s32 *)(arg0 + 0x1C8);
            temp_f0_4 = func_0037E908(func_00578598(temp_s1), dist[0]);
            M2C_FIELD(arg0, s32 *, 0x1CC) = 0;
            M2C_FIELD(arg0, f32 *, 0x1D0) = temp_f0_4;
            M2C_FIELD(arg0, f32 *, 0x1D4) = temp_f22;
            M2C_FIELD(arg0, f32 *, 0x1DC) = 1.0f;
            M2C_FIELD(arg0, f32 *, 0x1D8) = temp_f20_2;
            M2C_FIELD(arg0, f32 *, 0x1E0) = temp_f20_2;
            *var_s0 = 3;
            break;
        }
        default: break;
        }
    }

    temp_v1 = M2C_FIELD(arg0, s32 *, 0x1C4);
    if ((temp_v1 == 0) || ((temp_v1 == 1) && (var_f25 = M2C_FIELD(arg0, f32 *, 0x1F4), (temp_f0 < M2C_FIELD(arg0, f32 *, 0x1EC))))) {
        var_f25 = func_0036F880(arg0 + 0x1C8, temp_f24);
    }
    if (var_f25 <= 0x1.0000000000000p+1f) {
        var_f25 = 0x1.0000000000000p+1f;
    }
    if (var_f25 >= 0x1.1800000000000p+6f) {
        var_f25 = 0x1.1800000000000p+6f;
    }
    f32 *pdist = dist;
    func_003FC658(arg3, &rear[0]);
    func_005F5268(&rear[0], origin);
    var_f20 = func_0036EA98(&rear[0]);
    func_005F5040(ptarget, pdist);
    func_005F5268(pdist, origin);
    var_f20 = func_0036EB98(func_0036EA98(pdist), var_f20);
    if (M2C_FIELD(arg0, s32 *, 0x180) != 0) {
        if (var_f20 <= -0x1.9999980000000p-2f) {
            var_f20 = -0x1.9999980000000p-2f;
        }
        if (var_f20 >= 0x1.9999980000000p-2f) {
            var_f20 = 0x1.9999980000000p-2f;
        }
        var_s4 = (f32 *)(arg0 + 0x53C);
        func_00385058(var_s4, -var_f20, 0x1.4000000000000p+4f, -0x1.4000000000000p+4f, 0x1.0000000000000p+0f, 0x1.0000000000000p+1f, 0x1.e000000000000p+6f, 0x1.e000000000000p+6f);
    } else {
        if (var_f20 <= -0x1.9999980000000p-2f) {
            var_f20 = -0x1.9999980000000p-2f;
        }
        if (var_f20 >= 0x1.9999980000000p-2f) {
            var_f20 = 0x1.9999980000000p-2f;
        }
        var_s4 = (f32 *)(arg0 + 0x53C);
        func_00385098(var_s4, -var_f20);
    }
    temp_f0_7 = M2C_FIELD(arg0, f32 *, 0x24C);
    f32 *psx = component(scale, 0);
    f32 *psy = component(scale, 1);
    f32 *psz = component(scale, 2);
    *psx = 0x1.0000000000000p+0f;
    *psy = 0x1.0000000000000p+0f;
    *psz = 0x1.0000000000000p+0f;
    f32 fadeEnd = M2C_FIELD(arg0, f32 *, 0x250);
    if (temp_f0_7 < var_f26) {
        var_f1 = fadeRatio(var_f26, temp_f0_7, fadeEnd);
        if (var_f1 <= 0x1.0624dc0000000p-10f) {
            var_f1 = 0x1.0624dc0000000p-10f;
        }
        if (var_f1 >= 0x1.0000000000000p+0f) {
            var_f1 = 0x1.0000000000000p+0f;
        }
        *psx = var_f1;
        *psy = var_f1;
        *psz = var_f1;
    }
    if ((temp_f24 > 0x1.e666660000000p-1f) || (temp_f24 < 0x1.47ae140000000p-7f)) {
        func_005A48D8(psx, 0, 0xC);
    }
    temp_s1_2 = (f32 *)(arg0 + 0x4F4);
    if (M2C_FIELD(arg0, s32 *, 0x180) != 0) {
        func_00385058(temp_s1_2, *psx, 0x1.9000000000000p+5f, -0x1.4000000000000p+4f, 0x1.0000000000000p+0f, 0x1.8000000000000p+0f, 0x1.9000000000000p+6f, 0x1.9000000000000p+6f);
        func_00385058((var_s0_2 = (f32 *)(arg0 + 0x518)), *psy, 0x1.9000000000000p+5f, -0x1.4000000000000p+4f, 0x1.0000000000000p+0f, 0x1.8000000000000p+0f, 0x1.9000000000000p+6f, 0x1.9000000000000p+6f);
    } else {
        var_s0_2 = (f32 *)(arg0 + 0x518);
        func_00385098(temp_s1_2, *psx);
        func_00385098(var_s0_2, *psy);
    }
    f32 *poff = component(offset, 0);
    temp_f0_8 = adjustAngles(pax, pay, poff, psx, psy, var_s4, temp_s1_2, var_s0_2, var_f25, 0.5f, 0x1.333332p-2f);
    *pax = temp_f0_8;
    if (M2C_FIELD(arg0, s32 *, 0x180) != 0) {
        var_s4_2 = (f32 *)(arg0 + 0x258);
        func_00384E58(var_s4_2, M2C_FIELD(arg0, s32 *, 0x184), temp_f0_8, temp_f0_8, *psx);
        temp_f12 = *pay;
        var_s3 = (f32 *)(arg0 + 0x384);
        var_s1 = (f32 *)(arg0 + 0x584);
        func_00384E58(var_s3, M2C_FIELD(arg0, s32 *, 0x184), temp_f12, temp_f12, *psy);
        func_00385058(var_s1, M2C_FIELD(arg0, f32 *, 0x224), 0x1.e000000000000p+4f, -0x1.4000000000000p+3f, 0x1.0000000000000p+0f, 0, 0, 0x1.c200000000000p+9f);
        if (M2C_FIELD(arg0, s32 *, 0x1C0) != 0) {
            var_f25 *= (func_00578598(M2C_FIELD(arg0, s32 *, 0x184)) * 0x1.e000000000000p+3f) + 0x1.8000000000000p+1f;
            if (var_f25 <= 0x1.0000000000000p+1f) {
                var_f25 = 0x1.0000000000000p+1f;
            }
            if (var_f25 >= 0x1.6800000000000p+6f) {
                var_f25 = 0x1.6800000000000p+6f;
            }
        }
        var_s0_3 = (f32 *)(arg0 + 0x4B0);
        func_00385190(var_s0_3, var_f25, 0x1.9000000000000p+6f, -0x1.4000000000000p+4f, 0x1.0000000000000p+0f, 0x1.d1eb840000000p-1f, 0x1.07ae140000000p+0f);
    } else {
        var_s3 = (f32 *)(arg0 + 0x384);
        var_s4_2 = (f32 *)(arg0 + 0x258);
        var_s1 = (f32 *)(arg0 + 0x584);
        func_00384EE8(var_s4_2, M2C_FIELD(arg0, s32 *, 0x184), 1, temp_f0_8, *psx);
        var_s0_3 = (f32 *)(arg0 + 0x4B0);
        func_00384EE8(var_s3, M2C_FIELD(arg0, s32 *, 0x184), 1, *pay, *psy);
        func_00385098(var_s1, 0x0.0p+0f);
        func_003851D0(var_s0_3, var_f25);
    }
    *pax = *var_s4_2;
    *pay = *var_s3;
    f32 roll = func_0036F980(arg0 + 0x1FC, var_f26) + *var_s1;
    f32 *paz = component(angle, 2);
    *paz = roll;
    var_f25 = *var_s0_3;
    if (M2C_FIELD(arg0, s32 *, 0x180) != 0) {
        var_s0_4 = (f32 *)(arg0 + 0x4D0);
        func_00385058(var_s0_4, M2C_FIELD(arg0, f32 *, 0x254), 0x1.e000000000000p+3f, -0x1.4000000000000p+4f, 0x1.0000000000000p+0f, 0x1.0000000000000p+1f, 0x1.4000000000000p+4f, 0x1.4000000000000p+4f);
    } else {
        var_s0_4 = (f32 *)(arg0 + 0x4D0);
        func_00385098(var_s0_4, 0x1.0000000000000p+1f);
    }
    var_f21 = *var_s0_4;
    if (var_f21 <= 0x0.0p+0f) {
        var_f21 = 0x0.0p+0f;
    }
    if (var_f21 >= 0x1.0000000000000p+0f) {
        var_f21 = 0x1.0000000000000p+0f;
    }
    var_f21 = var_f21 * distance;
    if (var_f21 <= 0x1.0624dc0000000p-10f) {
        var_f21 = 0x1.0624dc0000000p-10f;
    }
    if (var_f21 >= 0x1.86a0000000000p+16f) {
        var_f21 = 0x1.86a0000000000p+16f;
    }
    f32 *poy = component(origin, 1);
    f32 *poz = component(origin, 2);
    setPose(arg0, -origin[0], -*poy, -*poz, *pax, *pay, *paz);
    M2C_FIELD(arg0, f32 *, 8) = var_f21;
    M2C_FIELD(arg0, f32 *, 0xC) = var_f25;
    func_005F5040(origin, arg0 + 0x1A8);
    func_005F5040(ptarget, arg0 + 0x19C);
    func_005F5040(angle, arg0 + 0x1B4);
    func_004A0690();
    func_004A53F8();
    func_004A7454();
    func_004A79D8(*paz);
    func_004A7988(*pax);
    func_004A79B0(*pay);
    func_004A7844(-origin[0], -*poy, -*poz);
    func_004A7550(arg0 + 0x100);
    func_004A5400();
    func_004A06F0();
    M2C_FIELD(arg0, s32 *, 0xEC) = 0x4000;
    M2C_FIELD(arg0, s32 *, 0x180) = 0;
    if (flag[0] != 0) {
        M2C_FIELD(arg0, s32 *, 0xE8) = 3;
    }
    return 1;
}
