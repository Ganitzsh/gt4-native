/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native correction of the provisional draft build/swarm/a7/0037ffb0_best.cpp (argument order, argcheck.py orders): func_0036F6A0 is (pointer, int, float) as defined; the draft put the float second, the same EE call but other stack slots natively. */
typedef signed char s8; typedef unsigned char u8; typedef short s16; typedef int s32; typedef unsigned int u32; typedef long long s64; typedef float f32; typedef int M2C_UNK;
#define M2C_FIELD(expr, type_ptr, offset) (*(type_ptr)((char *)(expr) + (offset)))
extern "C" {
f32 func_0036EA50(f32 *) throw();                           
f32 func_0036EA98(f32 *) throw();                           
f32 func_0036EB58(f32) throw();                             
s32 func_0036F600(s32) throw();                    
s32 func_0036F628(s32) throw();                    
f32 func_0036F650(s32) throw();                    
f32 func_0036F678(s32) throw();                    
f32 func_0036F6A0(void *, s32, f32) throw();                
void func_0037F4E0(void *, void *, void *, f32 *, f32 *, void *) throw(); 
s32 func_0037F640(void *, void *, void *, void *, f32) throw(); 
s32 func_0037F7F0(void *, void *, void *, void *) throw();    
s32 func_0037FA38(void *, f32 *, f32 *) throw();            
f32 func_0037FEE0(s32 *, s32 *, f32, f32, f32, f32) throw(); 
f32 func_0037FF48(f32, f32) throw() __attribute__((const));                        
s32 func_00380C50(void *, s32, void *, f32);        
f32 func_003FD3D8(s32, void *, void *, f32, f32, f32, f32); 
void func_004857C8(const char *); 
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
f32 func_0057D1F0(f32);                             
f32 func_0057D2B8(f32) throw();                             
void func_005A48D8(void *, s32, u32);     
void func_005F5040(void *, void *);           
void func_005F5230(void *, void *);      
void func_005F5268(f32 *, f32 *);                

}
static inline f32 *component(f32 *v, int i) { return v+i; }
extern "C" s32 func_0037FFB0(char *arg0, M2C_UNK arg1, char *arg2, char *arg3, char *arg4) {
    float pos[4], extra[4], angle[4];
    s32 limits[4];
    union { float f[4]; s32 i[4]; } scratch;
    f32 lens[4], v[4], w[4];
    char matrix[0x40];

    char *spC0;
    char *spC4;
    f32 spC8;
    f32 *spCC;
    f32 spD0;
    f32 spD4;
    f32 spD8;
    f32 spDC;
    f32 *spE0;
    f32 *spE4;
    f32 *spE8;
    f32 *temp_s7;
    f32 *var_fp;
    f32 *var_s5_2;
    f32 temp_f0;
    f32 temp_f0_2;
    f32 temp_f0_3;
    f32 temp_f0_4;
    f32 temp_f0_5;
    f32 temp_f0_6;
    f32 temp_f12;
    f32 temp_f15;
    f32 temp_f1;
    f32 temp_f20;
    f32 temp_f20_2;
    f32 temp_f20_3;
    f32 temp_f20_4;
    f32 temp_f20_5;
    f32 temp_f20_6;
    f32 temp_f21;
    f32 temp_f21_2;
    f32 temp_f21_3;
    f32 temp_f21_4;
    f32 temp_f21_5;
    f32 temp_f22;
    f32 temp_f24;
    f32 temp_f27;
    f32 temp_f28;
    f32 temp_f2;
    f32 temp_f2_2;
    f32 temp_f31;
    f32 temp_f3;
    f32 temp_f4;
    f32 temp_f5;
    f32 var_f0;
    f32 var_f20;
    f32 var_f24;
    f32 var_f25;
    s32 var_v1;
    s32 var_v1_2;
    s32 *temp_a1;
    s32 temp_a0;
    s32 temp_a0_10;
    s32 temp_a0_11;
    s32 temp_a0_12;
    s32 temp_a0_13;
    s32 temp_a0_2;
    s32 temp_a0_3;
    s32 temp_a0_4;
    s32 temp_a0_5;
    s32 temp_a0_6;
    s32 temp_a0_7;
    s32 temp_a0_8;
    s32 temp_a0_9;
    s32 temp_a2;
    s64 temp_a2_2;
    s64 temp_a2_3;
    s32 temp_a3;
    s32 temp_s1;
    s32 temp_v1;
    s32 var_a1;
    s32 var_a1_2;
    s32 var_a1_3;
    s32 var_a4;
    s32 var_s5;
    s32 var_v0;
    s32 var_v1_3;
    s32 var_v1_4;
    s64 temp_a3_2;
    s64 temp_v1_2;
    s64 temp_v1_3;
    s64 var_s0;
    s8 temp_s0;
    char *temp_s3;
    char *temp_v0;
    char *temp_v0_2;
    char *var_s4;

    f32 *pextra = extra;
    var_s5 = 0;
    temp_s3 = M2C_FIELD(arg0, char **, 0x184);
    spC0 = arg3;
    spC4 = arg4;
    spCC = (f32 *)(temp_s3 + 0x34);
    temp_s7 = (f32 *)(temp_s3 + 4);
    var_f24 = M2C_FIELD(temp_s3, f32 *, 0x40);
    temp_f24 = var_f24;
    spC8 = M2C_FIELD(temp_s3, f32 *, 0x1C);
    func_005F5040(temp_s7, pos);
    func_005F5040(spCC, &angle[0]);
    func_005F5040(arg0 + 0x188, pextra);
    if (M2C_FIELD(arg2, u8 *, 0x52) != 0) {
        if (M2C_FIELD(temp_s3, f32 *, 0x58) < 0x1p-1f) {
            M2C_FIELD(temp_s3, f32 *, 0x58) = 1.0f;
        } else {
            M2C_FIELD(temp_s3, f32 *, 0x58) = 0.0f;
        }
    }
    temp_s0 = M2C_FIELD(arg2, s8 *, 0x4C);
    int iy = 1;
    f32 *py = component(pos, iy);
    temp_f20 = *py;
    var_f20 = temp_f20 + (func_0036F6A0(arg0 + 0x1B6, temp_s0 != 0, 0x1.9999980000000p-5f) * (f32) temp_s0);
    temp_f2 = M2C_FIELD(temp_s3, f32 *, 0x2C);
    if (var_f20 < temp_f2) {
        var_f20 = temp_f2;
    }
    temp_f0 = M2C_FIELD(temp_s3, f32 *, 0x30);
    if (temp_f0 < var_f20) {
        var_f20 = temp_f0;
    }
    var_s0 = 0;
    temp_f21 = M2C_FIELD(arg2, f32 *, 0x48) * 0x1.9999980000000p-5f;
    temp_f12 = __builtin_fabsf(temp_f21);
    *py = var_f20;
    var_a1 = 1;
    if (temp_f21 == 0x0.0p+0f) {
        var_a1 = 0;
    }
    temp_f0_2 = func_0036F6A0(arg0 + 0x1B4, var_a1, temp_f12) + 0x1.0000000000000p+0f;
    if (temp_f21 < 0x0.0p+0f) {
        var_f24 /= temp_f0_2;
    } else {
        var_f24 *= temp_f0_2;
    }
    temp_f0_3 = M2C_FIELD(temp_s3, f32 *, 0x68);
    if (var_f24 < temp_f0_3) {
        var_f24 = temp_f0_3;
        var_s0 = 1;
    }
    if (spC8 < var_f24) {
        var_f24 = spC8;
        var_s0 = 1;
    }
    if (var_s0 != 0) {
        var_s5 = M2C_FIELD(arg0, u8 *, 0x1BC) == 0;
    }
    M2C_FIELD(arg0, s64 *, 0x1B8) = (s64) ((M2C_FIELD(arg0, s64 *, 0x1B8) & ((((-0x100LL << 0x10) | 0xFFFF) << 0x10) | 0xFFFF)) | (var_s0 << 0x20));
    if ((func_00380C50(arg0, M2C_FIELD(spC0, s32 *, 0x10), spC4, var_f24) == 0) && (M2C_FIELD(temp_s3, u8 *, 0x6C) == 0)) {
        if (M2C_FIELD(temp_s3, f32 *, 0x68) < var_f24) {
            M2C_FIELD(temp_s3, f32 *, 0x68) = var_f24;
        }
        var_f24 = temp_f24;
        M2C_FIELD(temp_s3, s32 *, 0x6C) = (s32) (((s32) M2C_FIELD(temp_s3, s32 *, 0x6C) & ~0xFF) | 1);
    }
    if (var_f24 < temp_f24) {
        M2C_FIELD(temp_s3, u8 *, 0x6D) = 0U;
    }
    spD4 = angle[0];
    spD8 = angle[0];
    spD0 = M2C_FIELD(arg0, f32 *, 0x1AC);
    temp_f28 = 0.0f;
    temp_f31 = 0.0f;
    spDC = func_0036EB58(angle[1]);
    var_a1_2 = 0;
    temp_f21_2 = M2C_FIELD(arg2, f32 *, 0x3C);
    f32 *pzangle = &angle[2];
    var_f25 = *pzangle;
    temp_f27 = M2C_FIELD(arg2, f32 *, 0x40);
    temp_f22 = var_f25;
    if ((temp_f21_2 != 0x0.0p+0f) || (temp_f27 != 0x0.0p+0f)) {
        var_a1_2 = 1;
    }
    var_s0 = 0;
    var_a1_3 = 1;
    f32 rollzero = 0.0f;
    temp_f1 = (func_0036F6A0(arg0 + 0x1B0, var_a1_2, 0x1.0000000000000p+1f) * var_f24) / 0x1.e000000000000p+5f;
    temp_f20_2 = M2C_FIELD(arg2, f32 *, 0x44);
    temp_f31 += temp_f1 * temp_f27;
    temp_f28 += temp_f1 * temp_f21_2;
    if (temp_f20_2 == rollzero) {
        var_a1_3 = 0;
    }
    temp_f0_4 = func_0036F6A0(arg0 + 0x1B2, var_a1_3, 0x1.0000000000000p+1f) * temp_f20_2;
    if (temp_f20_2 != rollzero) {
        if (temp_f22 == rollzero) {
            if (M2C_FIELD(arg0, s8 *, 0x1B8) <= 0) {
                var_f25 = temp_f22 + temp_f0_4;
            } else {
                M2C_FIELD(arg0, s8 *, 0x1B8)--;
            }
        } else {
            var_f25 = temp_f22 + temp_f0_4;
            f32 crosses = func_0037FF48(temp_f22, var_f25);
            var_v0 = crosses != 0.0f;
            if (var_v0 != 0) {
                var_f25 = 0x0.0p+0f;
                M2C_FIELD(arg0, s8 *, 0x1B8) = 0x1E;
            } else {
                goto block_45;
            }
        }
    } else {
block_45:
        M2C_FIELD(arg0, s8 *, 0x1B8) = 0;
    }
    if (var_f25 < -0x1.6800000000000p+6f) {
        var_f25 = -0x1.6800000000000p+6f;
        var_s0 = 1;
    }
    if (var_f25 > 0x1.6800000000000p+6f) {
        var_f25 = 0x1.6800000000000p+6f;
        var_s0 = 1;
    }
    if (var_s0 != 0) {
        var_s5 = (M2C_FIELD(arg0, u8 *, 0x1BD) == 0) ? 1 : var_s5;
    }
    M2C_FIELD(arg0, s64 *, 0x1B8) = (s64) ((M2C_FIELD(arg0, s64 *, 0x1B8) & ((((-0xFF01LL << 0x10) | 0xFFFF) << 0x10) | 0xFFFF)) | (var_s0 << 0x28));
    var_s0 = 0;
    if ((var_f24 != temp_f24) || (M2C_FIELD(arg0, s32 *, 0x1DC) != 0)) {
        var_s0 = 1;
    }
    func_005A48D8(&scratch.f[0], 0, 0x10);
    temp_f20_3 = (-var_f25 * 0x1.921fb40000000p+1f) / 0x1.6800000000000p+7f;
    temp_f21_3 = func_0057D2B8(temp_f20_3);
    temp_f0_5 = func_0057D1F0(temp_f20_3);
    temp_f20_4 = (temp_f28 * temp_f0_5) + (temp_f31 * temp_f21_3);
    temp_f21_4 = (temp_f28 * -temp_f21_3) + (temp_f31 * temp_f0_5);
    if ((temp_f20_4 != 0x0.0p+0f) || (var_s0 != 0)) {
        temp_f15 = spC8 * spD0 * 0x1.0000000000000p-1f;
        M2C_FIELD(temp_s3, u8 *, 0x6D) = (u8) (M2C_FIELD(temp_s3, u8 *, 0x6D) & 0xFC);
        spD4 = func_0037FEE0(&limits[0], &limits[1], spD8 + temp_f20_4, var_f24 * spD0, -temp_f15, temp_f15);
        if (limits[0] != 0) {
            var_v1 = 1;
            M2C_FIELD(temp_s3, u8 *, 0x6D) = (u8) (M2C_FIELD(temp_s3, u8 *, 0x6D) | 1);
            if (!(temp_f20_4 < 0x0.0p+0f)) {
                var_v1 = 0x0.0p+0f;
            }
            scratch.i[0] = var_v1;
        }
        var_v1_2 = 1;
        if (limits[1] != 0) {
            M2C_FIELD(temp_s3, u8 *, 0x6D) = (u8) (M2C_FIELD(temp_s3, u8 *, 0x6D) | 2);
            if (!(temp_f20_4 > 0x0.0p+0f)) {
                var_v1_2 = 0x0.0p+0f;
            }
            scratch.i[1] = var_v1_2;
        }
    }
    if ((temp_f21_4 != 0x0.0p+0f) || (var_s0 != 0)) {
        M2C_FIELD(temp_s3, u8 *, 0x6D) = (u8) (M2C_FIELD(temp_s3, u8 *, 0x6D) & 0xF3);
        spDC = func_0037FEE0(&limits[0], &limits[1], spDC + temp_f21_4, var_f24, M2C_FIELD(temp_s3, f32 *, 0x24), M2C_FIELD(temp_s3, f32 *, 0x28));
        if (limits[0] != 0) {
            var_v1_3 = 1;
            M2C_FIELD(temp_s3, u8 *, 0x6D) = (u8) (M2C_FIELD(temp_s3, u8 *, 0x6D) | 4);
            if (!(temp_f21_4 < 0x0.0p+0f)) {
                var_v1_3 = 0;
            }
            scratch.i[2] = var_v1_3;
        }
        var_v1_4 = 1;
        if (limits[1] != 0) {
            M2C_FIELD(temp_s3, u8 *, 0x6D) = (u8) (M2C_FIELD(temp_s3, u8 *, 0x6D) | 8);
            if (!(temp_f21_4 > 0x0.0p+0f)) {
                var_v1_4 = 0;
            }
            scratch.i[3] = var_v1_4;
        }
    }
    var_a4 = 0;
    do {
        temp_v1 = var_a4 * 4;
        var_a4 += 1;
        temp_a0 = temp_v1 + 0x1C0;
        temp_a2 = M2C_FIELD(&scratch, s32 *, temp_v1);
        temp_a1 = (s32 *)(arg0 + temp_a0);
        temp_a3 = var_a4 < 4;
        if (temp_a2 != 0) {
            var_s5 = (*temp_a1 == 0) ? 1 : var_s5;
        }
        M2C_FIELD(arg0, s32 *, temp_a0) = temp_a2;
    } while (temp_a3 != 0);
    angle[0] = spD4;
    angle[1] = spDC;
    *pzangle = var_f25;
    if (var_s5 != 0) {
        func_004857C8("disable");
    }
    temp_a0_2 = M2C_FIELD(temp_s3, s32 *, 0x48) + M2C_FIELD(arg2, s8 *, 0x50);
    temp_a0_3 = (temp_a0_2 <= 0) ? 0 : temp_a0_2;
    temp_a0_4 = (temp_a0_3 >= 0xF) ? 0xF : temp_a0_3;
    M2C_FIELD(temp_s3, s32 *, 0x48) = temp_a0_4;
    M2C_FIELD(arg0, s32 *, 0x150) = func_0036F600(temp_a0_4);
    temp_a0_5 = M2C_FIELD(temp_s3, s32 *, 0x4C) + M2C_FIELD(arg2, s8 *, 0x51);
    temp_a0_6 = (temp_a0_5 <= 0) ? 0 : temp_a0_5;
    temp_a0_7 = (temp_a0_6 >= 0x12) ? 0x12 : temp_a0_6;
    M2C_FIELD(temp_s3, s32 *, 0x4C) = temp_a0_7;
    M2C_FIELD(arg0, s32 *, 0x154) = func_0036F628(temp_a0_7);
    temp_a0_8 = M2C_FIELD(temp_s3, s32 *, 0x50) + M2C_FIELD(arg2, s8 *, 0x4E);
    temp_a0_9 = (temp_a0_8 <= 0) ? 0 : temp_a0_8;
    temp_a0_10 = (temp_a0_9 >= 0x14) ? 0x14 : temp_a0_9;
    M2C_FIELD(temp_s3, s32 *, 0x50) = temp_a0_10;
    M2C_FIELD(arg0, f32 *, 0x158) = func_0036F650(temp_a0_10);
    temp_a0_11 = M2C_FIELD(temp_s3, s32 *, 0x54) + M2C_FIELD(arg2, s8 *, 0x4F);
    temp_a0_12 = (temp_a0_11 <= 0) ? 0 : temp_a0_11;
    temp_a0_13 = (temp_a0_12 >= 0xA) ? 0xA : temp_a0_12;
    M2C_FIELD(temp_s3, s32 *, 0x54) = temp_a0_13;
    M2C_FIELD(arg0, f32 *, 0x15C) = func_0036F678(temp_a0_13);
    temp_v1_2 = M2C_FIELD(arg0, s64 *, 0x1B8) & ~0xFF00LL;
    M2C_FIELD(arg0, s64 *, 0x1B8) = temp_v1_2;
    if (M2C_FIELD(arg2, u8 *, 0x54) != 0) {
        M2C_FIELD(arg0, s64 *, 0x1B8) = (s64) ((temp_v1_2 & ~0xFF00LL) | 0x100);
        var_f0 = M2C_FIELD(spC4, f32 *, 0x50);
        if (var_f0 < 0x1.9999980000000p-4f) {
            var_f0 = 0x1.9999980000000p-4f;
        }
        var_s5_2 = (f32 *)(arg0 + 8);
        var_fp = (f32 *)(arg0 + 0x74);
        M2C_FIELD(arg0, f32 *, 8) = var_f0;
        M2C_FIELD(arg0, f32 *, 0x74) = var_f0;
        var_s4 = arg0 + 0x100;
        M2C_FIELD(temp_s3, f32 *, 0x44) = var_f0;
        M2C_FIELD(temp_s3, s32 *, 0x6C) = (s32) (((s32) M2C_FIELD(temp_s3, s32 *, 0x6C) & 0xFFFFFF) | 0x01000000);
        M2C_FIELD(temp_s3, s32 *, 0x70) = (s32) (M2C_FIELD(temp_s3, s32 *, 0x70) & ~0xFF);
        if (func_0037F640(spC0, pos, var_s4, &scratch.f[0], var_f0) != 0) {
            func_005F5040(&scratch.f[0], temp_s3 + 0x5C);
            M2C_FIELD(temp_s3, s32 *, 0x70) = (s32) ((M2C_FIELD(temp_s3, s32 *, 0x70) & ~0xFF) | 1);
        }
    } else {
        var_s5_2 = (f32 *)(arg0 + 8);
        var_fp = (f32 *)(arg0 + 0x74);
        var_s4 = arg0 + 0x100;
    }
    temp_a2_2 = (((-1LL << 0x10) | 0xFF) << 0x10) | 0xFFFF;
    temp_v1_3 = M2C_FIELD(arg0, s64 *, 0x1B8) & ~0xFF0000LL & temp_a2_2;
    M2C_FIELD(arg0, s64 *, 0x1B8) = temp_v1_3;
    if (M2C_FIELD(arg2, u8 *, 0x55) != 0) {
        M2C_FIELD(arg0, s64 *, 0x1B8) = (s64) ((temp_v1_3 & ~0xFF0000LL) | 0x10000);
    }
    if (M2C_FIELD(arg2, u8 *, 0x56) != 0) {
        M2C_FIELD(arg0, s64 *, 0x1B8) = (s64) ((((M2C_FIELD(arg0, s64 *, 0x1B8) & ~0xFF0000LL) | 0x10000) & temp_a2_2) | 0x01000000);
    }
    spE0 = (f32 *)(arg0 + 0x94);
    spE4 = (f32 *)(arg0 + 0x98);
    spE8 = (f32 *)(arg0 + 0x9C);
    temp_f20_5 = *pzangle + (M2C_FIELD(temp_s3, f32 *, 0x58) * 0x1.6800000000000p+6f);
    f32 *pa0 = (f32 *)(arg0 + 0xA0);
    f32 *pa4 = (f32 *)(arg0 + 0xA4);
    func_005F5040(pos, temp_s7);
    func_005F5040(&angle[0], spCC);
    M2C_FIELD(temp_s3, f32 *, 0x40) = var_f24;
    M2C_FIELD(arg0, f32 *, 0xC) = var_f24;
    temp_a2_3 = (((-0x100LL << 0x10) | 0xFFFF) << 0x10) | 0xFFFF;
    temp_a3_2 = 0x8000LL << 0x11;
    *var_s5_2 = M2C_FIELD(temp_s3, f32 *, 0x44);
    *var_fp = M2C_FIELD(temp_s3, f32 *, 0x44);
    M2C_FIELD(arg0, s32 *, 0x1E0) = (s32) M2C_FIELD(arg2, s8 *, 0x57);
    M2C_FIELD(arg0, f32 *, 0x1E8) = temp_f20_5;
    M2C_FIELD(arg0, s32 *, 0x1E4) = (s32) (s8) M2C_FIELD(temp_s3, u8 *, 0x6D);
    M2C_FIELD(arg0, s64 *, 0x168) = (s64) ((M2C_FIELD(arg0, s64 *, 0x168) & temp_a2_3) | temp_a3_2);
    M2C_FIELD(arg0, f32 *, 0x160) = (f32) M2C_FIELD(temp_s3, f32 *, 0x58);
    func_005F5230(&angle[0], temp_s3 + 0x10);
    f32 *py2 = &pos[1];
    f32 *pz2 = &pos[2];
    temp_f5 = -pos[0];
    temp_f4 = -*py2;
    temp_f3 = -*pz2;
    M2C_FIELD(arg0, f32 *, 0x34) = angle[0];
    M2C_FIELD(arg0, f32 *, 0x38) = angle[1];
    M2C_FIELD(arg0, f32 *, 0x28) = temp_f5;
    M2C_FIELD(arg0, f32 *, 0x2C) = temp_f4;
    M2C_FIELD(arg0, f32 *, 0x30) = temp_f3;
    M2C_FIELD(arg0, f32 *, 0x3C) = temp_f20_5;
    *pa0 = (f32) (angle[0] + M2C_FIELD(arg0, f32 *, 0x1A0));
    *pa4 = (f32) (angle[1] + M2C_FIELD(arg0, f32 *, 0x1A4));
    M2C_FIELD(arg0, f32 *, 0xA8) = temp_f20_5;
    *spE0 = temp_f5;
    *spE4 = temp_f4;
    *spE8 = temp_f3;
    func_004A0690();
    func_004A53F8();
    func_004A7454();
    func_004A79D8(temp_f20_5);
    func_004A7988(angle[1]);
    func_004A79B0(angle[0]);
    func_004A7844(-pos[0], -*py2, -*pz2);
    func_004A7550(var_s4);
    func_004A5400();
    func_004A06F0();
    M2C_FIELD(arg0, s32 *, 0xEC) = 0x4000;
    if (M2C_FIELD(temp_s3, u8 *, 0x6F) != 0) {
        if (M2C_FIELD(temp_s3, u8 *, 0x70) != 0) {
            func_0037F4E0(spC0, pos, temp_s3 + 0x5C, &lens[0], &lens[1], &scratch.f[0]);
            *var_s5_2 = lens[0];
            *var_fp = lens[1];
            *pa0 = (f32) (angle[0] + scratch.f[0]);
            *pa4 = (f32) (angle[1] + scratch.f[1]);
            if (M2C_FIELD(arg0, u8 *, 0x1BB) != 0) {
                f32 *pw = w;
                f32 *pv = v;
                func_005F5040(pos, pw);
                *pv = M2C_FIELD(arg0, f32 *, 0x108);
                v[1] = M2C_FIELD(arg0, f32 *, 0x118);
                v[2] = M2C_FIELD(arg0, f32 *, 0x128);
                temp_f2_2 = -lens[0];
                *pv *= temp_f2_2;
                v[1] *= temp_f2_2;
                v[2] *= temp_f2_2;
                func_005F5230(pv, pos);
                if (func_0037FA38(spC0, pw, pv) != 0) {
                    func_005F5268(pv, pw);
                    *pa0 = func_0036EA98(pv);
                    *pa4 = func_0036EA50(pv);
                    *spE0 = -*pw;
                    *spE4 = -w[1];
                    *spE8 = -w[2];
                    *var_fp = lens[0];
                }
            }
        } else {
            *pa0 = angle[0];
            *pa4 = angle[1];
        }
    } else {
        temp_s1 = M2C_FIELD(spC0, s32 *, 0x10);
        if (temp_s1 != 0) {
            temp_v0 = M2C_FIELD(arg0, char **, 0);
            temp_f21_5 = M2C_FIELD(arg0, f32 *, 0x1AC);
            temp_f20_6 = (f32) M2C_FIELD(arg0, s32 *, 0x150) / 0x1.9000000000000p+6f;
            M2C_FIELD((temp_v0 + 0x60), void (**)(char *), 4)(arg0 + M2C_FIELD(temp_v0, s16 *, 0x60));
            temp_v0_2 = M2C_FIELD(arg0, char **, 0);
            M2C_FIELD((temp_v0_2 + 0x78), void (**)(char *, void *, char *), 4)(arg0 + M2C_FIELD(temp_v0_2, s16 *, 0x78), matrix, spC4);
            temp_f0_6 = func_003FD3D8(temp_s1, matrix, var_s4, temp_f20_6, 0x1.1800000000000p+5f, temp_f21_5, 0x1.11111p-10f);
            *var_s5_2 = temp_f0_6;
            *var_fp = temp_f0_6;
            M2C_FIELD(temp_s3, f32 *, 0x44) = temp_f0_6;
            if (func_0037F7F0(spC0, pos, var_s4, &scratch.f[0]) != 0) {
                *pa0 = (f32) (angle[0] + scratch.f[0]);
                *pa4 = (f32) (angle[1] + scratch.f[1]);
            }
        }
    }
    M2C_FIELD(arg0, s32 *, 0x1DC) = 0;
    return 1;
}
