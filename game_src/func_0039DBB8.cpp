/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native copy of the provisional draft for 0039DBB8: the slot-0x48 virtual gets fparg0, which the EE forwards to it in $f12 (passthrough_lint) */
typedef signed char s8; typedef unsigned char u8; typedef short s16; typedef unsigned short u16;
typedef int s32; typedef unsigned int u32; typedef long long s64; typedef unsigned long long u64;
typedef float f32; typedef double f64;
typedef int s128 __attribute__((mode(TI))); typedef unsigned int u128 __attribute__((mode(TI)));
#define NULL 0
char *memcpy(char *, const char *, unsigned int);
/*
 * This header contains macros emitted by m2c in "valid syntax" mode,
 * which can be enabled by passing `--valid-syntax` on the command line.
 *
 * In this mode, unhandled types and expressions are emitted as macros so
 * that the output is compilable without human intervention.
 */

#ifndef M2C_MACROS_H
#define M2C_MACROS_H

/* Unknown types */
typedef s32 M2C_UNK;
typedef s8  M2C_UNK8;
typedef s16 M2C_UNK16;
typedef s32 M2C_UNK32;
typedef s64 M2C_UNK64;

/* Unknown field access, like `*(type_ptr) &expr->unk_offset` */
#define M2C_FIELD(expr, type_ptr, offset) (*(type_ptr)((s8 *)(expr) + (offset)))

/* Bitwise (reinterpret) cast */
#define M2C_BITWISE(type, expr) ((type)(expr))

/* Unaligned reads */
#define M2C_LWL(expr) (expr)
#define M2C_FIRST3BYTES(expr) (expr)
#define M2C_UNALIGNED32(expr) (expr)

/* Unhandled instructions */
#define M2C_ERROR(desc) (0)
#define M2C_TRAP_IF(cond) (0)
#define M2C_BREAK() (0)
#define M2C_SYNC() (0)
#define M2C_DCACHE_CLEAN(addr) (0)
#define M2C_DCACHE_INVALIDATE(addr) (0)
#define M2C_DCACHE_CLEAN_INVALIDATE(addr) (0)
#define M2C_DCACHE_BLOCK_SETZERO(addr) (0)
#define M2C_DCACHE_BLOCK_SETZERO_LOCKED(addr) (0)
#define M2C_ICACHE_INVALIDATE(addr) (0)
#define M2C_PREFETCH(addr) (0)
#define M2C_PREFETCH_STORE(addr) (0)

#define GLUE_F64(a, b) (0.0)
#define MULT_HI(a, b) (0)
#define MULTU_HI(a, b) (0)
#define DMULT_HI(a, b) (0)
#define DMULTU_HI(a, b) (0)
#define CLZ(x) (0)
#define REVERSE_BITS(x) (0)
#define ROTATE_RIGHT(x, shift) (0)
#define ARM_RRX(x, carry) (0)
#define BSWAP32(x) (0)
#define BSWAP16(x) (0)
#define BSWAP16X2(x) (0)

/* Carry/overflow bits from partially-implemented instructions */
#define M2C_CARRY 0
#define M2C_OVERFLOW(a) (0)

/* Memcpy patterns */
#define M2C_MEMCPY_ALIGNED memcpy
#define M2C_MEMCPY_UNALIGNED memcpy
#define M2C_STRUCT_COPY memcpy

/* Sh2 control register loads/stores */
#define M2C_LOAD_SR() (0)
#define M2C_LOAD_GBR() (0)
#define M2C_LOAD_VBR() (0)
#define M2C_STORE_SR(a)
#define M2C_STORE_GBR(a)
#define M2C_STORE_VBR(a)

#define M2C_CMP_STR(a, b) (0)
#define M2C_TAS_B(a) (0)

#endif


struct VE0 { short delta, index; void (*fn)(char *); };
struct VE1 { short delta, index; void (*fn)(char *, char *); };
struct VE2 { short delta, index; void (*fn)(char *, char *, f32); };
struct VE3 { short delta, index; void (*fn)(char *, f32); };
struct VE4 { short delta, index; void (*fn)(char *, int); };
struct VE5 { short delta, index; void (*fn)(char *, u32); };
struct VE6 { short delta, index; void (*fn)(char *, s32, u8, u8, u8); };
template<int off> static inline void setByte(char *p, int v) { p[off] = v; }
static inline void callChange(char *p) { VE0 *e = (VE0 *)(M2C_FIELD(p,char **,0)+0x1B0); e->fn(p+e->delta); }
static inline void setAlpha(char *p, float a) { M2C_FIELD(p,float *,12)=a; }
static inline int isPlay(char *p) { return M2C_FIELD(p,unsigned *,32) < 2; }
static inline int isReplay(char *p) { return isPlay(p) ^ 1; }
static inline float alpha(char *p) { return M2C_FIELD(p,int *,0x28) == 1 ? 1.0f : M2C_FIELD(p,float *,0x84); }
static inline void active(char *p,int a) { M2C_FIELD(p,unsigned *,0x18) = (M2C_FIELD(p,unsigned *,0x18) & 0xffff00ff) | (a << 8); }
static inline int countEvents(char *p) { return M2C_FIELD(p,u8 *,0x916); }
extern "C" {
extern char *D_00622F4C;
extern int D_006244D8[];
extern int D_006A0690[];
extern int D_006A06A0[];
u32 func_003434C8(char *);                          /* extern */
s32 func_00343B50(char *);                          /* extern */
s32 func_00343BE0(char *);                          /* extern */
s32 func_00344630(char *);                          /* extern */
s32 func_00344638(char *);                  /* extern */
s32 func_00344640(char *);                  /* extern */
s32 func_00344658(char *);                          /* extern */
f32 func_00344680(char *, char *);                  /* extern */
f32 func_00344698(char *);                          /* extern */
f32 func_003446A0(char *);                          /* extern */
f32 func_003446B0(char *);                          /* extern */
void func_003446D8(char *, s32, u8 *, u8 *, u8 *); /* extern */
f32 func_003451B0(char *);                          /* extern */
s32 func_003451D8(char *);                          /* extern */
s32 func_00350F30(char *);                          /* extern */
s32 func_00354E38(char *);                          /* extern */
f32 func_00354E58(char *);                          /* extern */
f32 func_00354E60(char *);             /* extern */
s32 func_00354E68(char *);                          /* extern */
f32 func_00354E98(char *);                          /* extern */
f32 func_00354EA8(char *);                          /* extern */
u8 *func_00359538(char *);                          /* extern */
s32 func_0035BEB0(char *);                          /* extern */
s32 func_00367D40(char *);                             /* extern */
s32 func_003683A8(char *);                          /* extern */
void func_00368420(char *, s8 *);                /* extern */
s32 func_00368498(char *);                          /* extern */
s32 func_003684E8(char *);                          /* extern */
f32 func_00368518(char *);                          /* extern */
void func_0039BBE0(char *) throw();                            /* extern */
void func_0039BCA8(char *) throw();                  /* extern */
void func_0039BD50(char *) throw();                      /* extern */
void func_0039CDA8(char *, int, float);    /* extern */
void func_0039E9B0(char *);                      /* extern */
void func_0039EC40(char *);                      /* extern */
void func_0039ECD0(char *, char *);              /* extern */
void func_0039EE78(char *);                      /* extern */
s8 func_0039F0C0(char *, M2C_UNK);                  /* extern */
void func_0039F200(char *, char *, s8);          /* extern */
void func_0039F368(char *, char *, s8);          /* extern */
void func_003A0308(char *, char *, s8);          /* extern */
void func_003A04B0(char *, char *, s8);          /* extern */
void func_003A0600(char *, char *, s8);          /* extern */
void func_003A06B0(char *, char *);              /* extern */
void func_003A0900(char *, char *);              /* extern */
void func_003A1330(char *);                      /* extern */
s32 func_003A1628(char *);                          /* extern */
void func_003A39B0(char *);                      /* extern */
void func_003A4AA8(char *, f32);                 /* extern */
void func_003A4CF0(char *, s32, float, float); /* extern */
void func_003A4F00(char *, float);        /* extern */
void func_003A4FC8(char *, f32);                 /* extern */
void func_003A6638(char *, f32);                 /* extern */
void func_003A9780(char *, f32);                 /* extern */
void func_003AA850(char *, s8);                  /* extern */
void func_003AA8D0(char *, s8);                  /* extern */
void func_003AA9B8(char *, s8);                  /* extern */
void func_003AAA38(char *, s8);                  /* extern */
void func_003AAF40(char *, s32);                 /* extern */
void func_003ADFD8(char *, f32);                 /* extern */
void func_003AE330(char *, f32);                 /* extern */
s32 func_003B7668(char *, M2C_UNK, M2C_UNK, M2C_UNK); /* extern */
s32 func_003B77E0(char *, M2C_UNK, char *, s32);    /* extern */
void func_003BEE58(char *, f32);                 /* extern */
s32 func_003D8B48(char *);                          /* extern */
void func_003DD7D8(char *, f32);                 /* extern */
s32 func_003F3758(char *);                          /* extern */
f32 func_00472940(int *, f32);                    /* extern */

void func_0039DBB8(char *arg0, f32 fparg0) {
    struct { int event; u8 r,g,b; } local;
    s8 gears[4];
#define sp ((char *)&local.event)
#define sp4 local.r
#define sp5 local.g
#define sp6 local.b
#define sp10 gears[0]
#define sp11 gears[1]
#define sp12 gears[2]
#define sp13 gears[3]
    char *sp20;
    float *fader;
    void (*temp_a2_6)(char *, u32);
    M2C_UNK var_a1;
    f32 temp_f0;
    f32 temp_f0_2;
    f32 var_f0;
    s32 temp_a1_10;
    s32 temp_a1_11;
    s32 temp_a1_12;
    s32 temp_a1_13;
    s32 temp_a1_9;
    s32 temp_v0_18;
    s32 temp_v0_19;
    s32 temp_v0_25;
    s32 temp_v0_33;
    s32 var_a1_2;
    s32 var_a1_3;
    s32 var_a1_4;
    s32 var_a1_5;
    s32 var_s0;
    s32 var_s0_2;
    s32 var_s0_3;
    s32 var_s1;
    s32 var_s1_2;
    s32 var_v0_2;
    s32 var_v0_3;
    s32 var_v1;
    s8 temp_s0_2;
    u32 temp_lo;
    u8 temp_a1;
    u8 temp_s0_3;
    u8 temp_v1;
    int temp_v1_3;
    char *temp_a0;
    char *temp_a0_2;
    char *temp_a0_3;
    char *temp_a0_4;
    char *temp_a0_5;
    char *temp_a0_6;
    char *temp_a0_7;
    char *temp_a1_14;
    char *temp_a1_15;
    char *temp_a1_16;
    char *temp_a1_17;
    char *temp_a1_18;
    char *temp_a1_19;
    char *temp_a1_2;
    char *temp_a1_3;
    char *temp_a1_4;
    char *temp_a1_5;
    char *temp_a1_6;
    char *temp_a1_7;
    char *temp_a1_8;
    char *temp_a2;
    char *temp_a2_2;
    char *temp_a2_3;
    char *temp_a2_4;
    char *temp_a2_5;
    char *temp_a2_7;
    char *temp_a2_8;
    char *temp_a2_9;
    char *temp_a3;
    char *temp_a5;
    char *temp_a5_2;
    char *temp_a5_3;
    char *temp_a5_4;
    char *temp_s0;
    char *temp_s0_4;
    char *temp_s0_5;
    char *temp_s0_6;
    char *temp_s0_7;
    char *temp_s1;
    char *temp_s1_2;
    char *temp_s1_3;
    char *temp_s1_4;
    char *temp_s2;
    char *temp_s2_2;
    char *temp_s2_3;
    char *temp_s4;
    char *temp_s5;
    VE0 *temp_v0;
    VE3 *temp_v0_10;
    VE3 *temp_v0_11;
    VE4 *temp_v0_12;
    VE4 *temp_v0_13;
    VE4 *temp_v0_14;
    VE4 *temp_v0_15;
    VE4 *temp_v0_16;
    VE3 *temp_v0_17;
    VE4 *temp_v0_20;
    VE6 *temp_v0_21;
    VE6 *temp_v0_22;
    VE6 *temp_v0_23;
    VE6 *temp_v0_24;
    VE3 *temp_v0_26;
    VE3 *temp_v0_27;
    VE3 *temp_v0_28;
    VE3 *temp_v0_29;
    char *temp_v0_2;
    VE4 *temp_v0_30;
    VE3 *temp_v0_31;
    VE3 *temp_v0_32;
    char *temp_v0_34;
    VE0 *temp_v0_3;
    VE1 *temp_v0_4;
    VE1 *temp_v0_5;
    VE1 *temp_v0_6;
    VE2 *temp_v0_7;
    VE3 *temp_v0_8;
    VE3 *temp_v0_9;
    VE5 *temp_v1_2;
    char *temp_v1_4;
    char *var_v0;

    if (M2C_FIELD(arg0, u8 *, 0x3E) == 0) {
        temp_v0 = (VE0 *)((char *)M2C_FIELD(arg0, char **, 0) + 0x48);
        ((void (*)(char *, f32))temp_v0->fn)(arg0 + temp_v0->delta, fparg0);
    } else if (M2C_FIELD(arg0, u8 *, 0x3F) != 0) {
        func_0039BBE0(arg0);
    }
    func_0039EC40(arg0);
    if (M2C_FIELD(arg0, u8 *, 0x40) == 0) {
        M2C_FIELD(arg0, s32 *, 0x40) = (s32) (((s32) M2C_FIELD(arg0, s32 *, 0x40) & ~0xFF) | 1);
        func_0039BD50(arg0);
    }
    temp_v0_2 = M2C_FIELD(arg0, char **, 0xC);
    fader = (float *)(arg0 + 0xBC);
    if (temp_v0_2 != NULL) {
        M2C_FIELD(temp_v0_2, f32 *, 0x84) = (f32) *fader;
        temp_a0 = M2C_FIELD(arg0, char **, 0xC);
        var_v1 = M2C_FIELD(temp_a0, s32 *, 0x28);
        if (M2C_FIELD(temp_a0, s32 *, 0x30) != 0) {
            switch (M2C_FIELD(temp_a0, s32 *, 0x28)) {
            case 0:
                func_0039CDA8(arg0, 0, 0.25f);
                callChange(arg0);
                break;
            case 1: break;
            case 2:
                func_0039CDA8(arg0, 1, 0.25f);
                callChange(arg0);
                break;
            }
        }
        var_v0 = arg0 + 0x2DA0;
        sp20 = var_v0;
        setAlpha(sp20, alpha(M2C_FIELD(arg0,char **,0xC)));
    } else {
        sp20 = arg0 + 0x2DA0;
    }
    if (M2C_FIELD(arg0, u8 *, 0x44) != 0) {
        M2C_FIELD(arg0, s32 *, 0x44) = (s32) ((s32) M2C_FIELD(arg0, s32 *, 0x44) & ~0xFF);
        temp_s0 = arg0 + 0x2DDC;
        func_003A4F00(arg0 + 0x2F2C, 0x1.999998p-4f);
        func_003A4CF0(temp_s0, M2C_FIELD(arg0, s32 *, 0x4C), 0, 0);
        if (M2C_FIELD(arg0, s32 *, 0x4C) == 0) {
            temp_a0_2 = M2C_FIELD(arg0, char **, 0xC);
            if (temp_a0_2 != NULL) {
                func_003A4CF0(temp_s0, func_003D8B48(temp_a0_2), 0, 0);
            }
        }
    } else if (M2C_FIELD(arg0, s32 *, 0x4C) == 0) {
        temp_a0_3 = M2C_FIELD(arg0, char **, 0xC);
        if (temp_a0_3 != NULL) {
            var_a1_2 = 1;
            if (M2C_FIELD(temp_a0_3, u8 *, 0xA9) != 0) {
                M2C_FIELD(temp_a0_3, s32 *, 0xA8) = (s32) (M2C_FIELD(temp_a0_3, s32 *, 0xA8) & 0xFFFF00FF);
            } else {
                var_a1_2 = 0;
            }
            if (var_a1_2 != 0) {
                func_003A4F00(arg0 + 0x2F2C, 0x1.999998p-4f);
                func_003A4CF0(arg0 + 0x2DDC, func_003D8B48(M2C_FIELD(arg0, char **, 0xC)), 0, 0);
            }
        }
    }
    temp_s5 = M2C_FIELD(arg0, char **, 4);
    temp_s0_2 = M2C_FIELD(arg0, s8 *, 0x2E);
    func_0039F200(arg0, temp_s5, temp_s0_2);
    M2C_FIELD(arg0, s32 *, 0x1584) = (s32) M2C_FIELD(temp_s5, s32 *, 0x968);
    func_0039F368(arg0, temp_s5, temp_s0_2);
    if (func_003A1628(arg0) != 0) {
        M2C_FIELD(arg0, u8 *, 0x47) <<= 1;
        if (countEvents(temp_s5) > 0) {
            M2C_FIELD(arg0, u8 *, 0x47) = M2C_FIELD(arg0, u8 *, 0x47) | 1;
        }
        if (M2C_FIELD(arg0, u8 *, 0x47) == 1) {
            func_003A1330(arg0);
        }
    }
    var_s1 = 0;
    func_003A0308(arg0, temp_s5, temp_s0_2);
    func_003A04B0(arg0, temp_s5, temp_s0_2);
    func_003A0600(arg0, temp_s5, temp_s0_2);
    temp_s2 = temp_s5 + 0xF4;
    func_003A06B0(arg0, temp_s5);
    func_003A0900(arg0, temp_s5);
    func_0039E9B0(arg0);
    temp_s0_3 = M2C_FIELD(temp_s5, u8 *, 0x906);
    if (temp_s0_3 != 0) {
        do {
            int found = func_003B77E0(temp_s2, 0xA, sp, var_s1);
            var_s1 += 1;
            if (found != 0) {
                temp_v0_4 = (VE1 *)((char *)M2C_FIELD(arg0, char **, 0) + 0x128);
                temp_v0_4->fn(arg0 + temp_v0_4->delta, temp_s5);
            }
        } while (var_s1 < (s32) temp_s0_3);
    }
    if (func_003B7668(temp_s2, 9, 0, 0) != 0) {
        temp_v0_5 = (VE1 *)((char *)M2C_FIELD(arg0, char **, 0) + 0x130);
        temp_v0_5->fn(arg0 + temp_v0_5->delta, temp_s5);
    }
    temp_a1 = M2C_FIELD(arg0, u8 *, 0x33);
    if (temp_a1 != M2C_FIELD(D_00622F4C, s32 *, 0x39DF4)) {
        func_0039BCA8(arg0);
    }
    temp_s1 = ((char **)M2C_FIELD(M2C_FIELD(temp_s5, char **, 0x60), char **, 8))[M2C_FIELD(arg0, s8 *, 0x2E)];
    temp_s4 = M2C_FIELD(temp_s1, char **, 0x18);
    if ((M2C_FIELD(temp_s5, s32 *, 0xC4) == 0) || ((s32) M2C_FIELD(temp_s5, f32 *, 0xC8) >= 0x3C)) {
        temp_v0_6 = (VE1 *)((char *)M2C_FIELD(arg0, char **, 0) + 0xD8);
        temp_v0_6->fn(arg0 + temp_v0_6->delta, temp_s4);
    }
    temp_s0_4 = arg0 + 0x228C;
    func_003A4AA8(temp_s0_4, fparg0);
    M2C_FIELD(temp_s0_4, f32 *, 0xC) = (f32) *fader;
    temp_v0_7 = (VE2 *)((char *)M2C_FIELD(arg0, char **, 0) + 0xD0);
    temp_v0_7->fn(arg0 + temp_v0_7->delta, temp_s5, fparg0);
    func_003AAF40(arg0 + 0x1300, func_00343B50(temp_s4));
    func_0039ECD0(arg0, temp_s1);
    func_003A9780((char *)fader, fparg0);
    temp_a1_2 = M2C_FIELD(arg0, char **, 0x14);
    if (temp_a1_2 != NULL) {
        temp_v0_8 = (VE3 *)((char *)M2C_FIELD(temp_a1_2, char **, 0x14) + 0x50);
        temp_s0_5 = temp_s4 + 0x14;
        temp_v0_8->fn(temp_a1_2 + temp_v0_8->delta, fparg0);
        float value_434 = (f32) func_00350F30(temp_s4);
        temp_a1_3 = M2C_FIELD(arg0, char **, 0x14);
        temp_v0_9 = (VE3 *)((char *)M2C_FIELD(temp_a1_3, char **, 0x14) + 0x78);
        temp_v0_9->fn(temp_a1_3 + temp_v0_9->delta, value_434);
        float value_437 = func_00472940(D_006244D8, M2C_FIELD(temp_s4, f32 *, 0x6C8));
        temp_a1_4 = M2C_FIELD(arg0, char **, 0x14);
        temp_v0_10 = (VE3 *)((char *)M2C_FIELD(temp_a1_4, char **, 0x14) + 0x68);
        temp_v0_10->fn(temp_a1_4 + temp_v0_10->delta, value_437);
        float value_440 = func_003446B0(temp_s4);
        temp_a1_5 = M2C_FIELD(arg0, char **, 0x14);
        temp_v0_11 = (VE3 *)((char *)M2C_FIELD(temp_a1_5, char **, 0x14) + 0x90);
        temp_v0_11->fn(temp_a1_5 + temp_v0_11->delta, value_440);
        temp_a2 = M2C_FIELD(arg0, char **, 0x14);
        temp_v0_12 = (VE4 *)((char *)M2C_FIELD(temp_a2, char **, 0x14) + 0x98);
        temp_v0_12->fn(temp_a2 + temp_v0_12->delta, M2C_FIELD(temp_s4, u8 *, 0x624));
        temp_a2_2 = M2C_FIELD(arg0, char **, 0x14);
        temp_v0_13 = (VE4 *)((char *)M2C_FIELD(temp_a2_2, char **, 0x14) + 0xC0);
        temp_v0_13->fn(temp_a2_2 + temp_v0_13->delta, M2C_FIELD(temp_s0_5, s8 *, 0x40));
        int value_449 = func_00343BE0(temp_s4);
        temp_a2_3 = M2C_FIELD(arg0, char **, 0x14);
        temp_v0_14 = (VE4 *)((char *)M2C_FIELD(temp_a2_3, char **, 0x14) + 0xA0);
        temp_v0_14->fn(temp_a2_3 + temp_v0_14->delta, value_449);
        int value_452 = func_003F3758(temp_s4) != 0;
        temp_a2_4 = M2C_FIELD(arg0, char **, 0x14);
        temp_v0_15 = (VE4 *)((char *)M2C_FIELD(temp_a2_4, char **, 0x14) + 0xB0);
        temp_v0_15->fn(temp_a2_4 + temp_v0_15->delta, value_452);
        int flag41 = M2C_FIELD(temp_s0_5,s8 *,0x41) != 0;
        temp_a2_5 = M2C_FIELD(arg0, char **, 0x14);
        temp_v0_16 = (VE4 *)((char *)M2C_FIELD(temp_a2_5, char **, 0x14) + 0xC8);
        temp_v0_16->fn(temp_a2_5 + temp_v0_16->delta, flag41);
        float value_458 = -func_003451B0(temp_s4) * 0x1.a1f58e0000000p-4f;
        temp_a1_6 = M2C_FIELD(arg0, char **, 0x14);
        temp_v0_17 = (VE3 *)((char *)M2C_FIELD(temp_a1_6, char **, 0x14) + 0xD8);
        temp_v0_17->fn(temp_a1_6 + temp_v0_17->delta, value_458);
        temp_s1_2 = M2C_FIELD(arg0, char **, 0x14);
        M2C_FIELD(temp_s1_2, f32 *, 0x24) = func_00344698(temp_s4);
        temp_s0_6 = M2C_FIELD(arg0, char **, 0x14);
        M2C_FIELD(temp_s0_6, f32 *, 0x28) = func_003446A0(temp_s4);
        temp_lo = func_003434C8(temp_s4 + 0x6DC) / 10U;
        temp_a3 = M2C_FIELD(arg0, char **, 0x14);
        temp_v1_2 = (VE5 *)((char *)M2C_FIELD(temp_a3, char **, 0x14) + 0xE0);
        temp_a2_6 = temp_v1_2->fn;
        temp_a2_6(temp_a3 + temp_v1_2->delta, ((s32) temp_lo <= 0xF423F) ? temp_lo : 0xF423FU);
        func_00344658(temp_s4);
        temp_v0_18 = func_00344630(temp_s4);
        temp_a1_7 = M2C_FIELD(arg0, char **, 0x14);
        var_s1_2 = temp_v0_18;
        setByte<0x1F>(temp_a1_7, D_006A0690[var_s1_2]);
        if (temp_v0_18 < func_00344638(temp_s4)) {
            var_s1_2 = func_00344638(temp_s4);
        }
        temp_a1_8 = M2C_FIELD(arg0, char **, 0x14);
        setByte<0x1E>(temp_a1_8, D_006A0690[var_s1_2]);
        temp_s2_2 = M2C_FIELD(arg0, char **, 0x14);
        setByte<0x20>(temp_s2_2, D_006A06A0[func_00344640(temp_s4)]);
        temp_s1_3 = M2C_FIELD(arg0, char **, 0x14);
        setByte<0x21>(temp_s1_3, ((func_00354E68(temp_s4) != 0) ? 3 : 0));
        temp_s2_3 = M2C_FIELD(arg0, char **, 0x14);
        setByte<0x22>(temp_s2_3, D_006A06A0[func_00344658(temp_s4)]);
        if (func_003451D8(temp_s4) != 0) {
            temp_a0_4 = M2C_FIELD(arg0, char **, 0x14);
            active(temp_a0_4, 1);
            temp_v0_19 = func_00367D40(M2C_FIELD(arg0, char **, 0x10) + 4);
            temp_a2_7 = M2C_FIELD(arg0, char **, 0x14);
            temp_v0_20 = (VE4 *)((char *)M2C_FIELD(temp_a2_7, char **, 0x14) + 0xE8);
            temp_v0_20->fn(temp_a2_7 + temp_v0_20->delta, temp_v0_19);
            if (temp_v0_19 != 0) {
                temp_v1_3 = *func_00359538(temp_s4);
                switch (temp_v1_3) {
                case 0:
                    for (int i=0; i<4; i++) {
                        func_003446D8(temp_s4, i, &sp4, &sp5, &sp6);
                        char *panel = M2C_FIELD(arg0, char **, 0x14);
                        VE6 *e = (VE6 *)(M2C_FIELD(panel, char **, 0x14) + 0xF0);
                        e->fn(panel + e->delta, i, sp4, sp5, sp6);
                    }
                    break;
                case 2:
                    for (int i=0; i<4; i++) {
                        func_003446D8(temp_s4, i < 2 ? 0 : i - 1, &sp6, &sp5, &sp4);
                        char *panel = M2C_FIELD(arg0, char **, 0x14);
                        VE6 *e = (VE6 *)(M2C_FIELD(panel, char **, 0x14) + 0xF0);
                        e->fn(panel + e->delta, i, sp6, sp5, sp4);
                    }
                    break;
                case 3:
                    for (int i=0; i<4; i++) {
                        func_003446D8(temp_s4, i < 3 ? i : 2, &sp6, &sp5, &sp4);
                        char *panel = M2C_FIELD(arg0, char **, 0x14);
                        VE6 *e = (VE6 *)(M2C_FIELD(panel, char **, 0x14) + 0xF0);
                        e->fn(panel + e->delta, i, sp6, sp5, sp4);
                    }
                    break;
                case 1:
                    for (int i=0; i<4; i++) {
                        func_003446D8(temp_s4, i >> 1, &sp6, &sp5, &sp4);
                        char *panel = M2C_FIELD(arg0, char **, 0x14);
                        VE6 *e = (VE6 *)(M2C_FIELD(panel, char **, 0x14) + 0xF0);
                        e->fn(panel + e->delta, i, sp6, sp5, sp4);
                    }
                    break;
                }
            }
            temp_v0_25 = func_00354E38(temp_s4);
            temp_a2_8 = M2C_FIELD(arg0, char **, 0x14);
            temp_a1_13 = temp_v0_25 << 0x18;
            M2C_FIELD(temp_a2_8, s32 *, 0x18) = (s32) ((M2C_FIELD(temp_a2_8, s32 *, 0x18) & 0xFFFFFF) | temp_a1_13);
            if (temp_v0_25 != 0) {
        float value_558 = func_00354E60(temp_s4);
                temp_a1_14 = M2C_FIELD(arg0, char **, 0x14);
                temp_v0_26 = (VE3 *)((char *)M2C_FIELD(temp_a1_14, char **, 0x14) + 0xF8);
                temp_v0_26->fn(temp_a1_14 + temp_v0_26->delta, value_558);
        float value_561 = func_00354E58(temp_s4);
                temp_a1_15 = M2C_FIELD(arg0, char **, 0x14);
                temp_v0_27 = (VE3 *)((char *)M2C_FIELD(temp_a1_15, char **, 0x14) + 0x100);
                temp_v0_27->fn(temp_a1_15 + temp_v0_27->delta, value_561);
            }
            temp_f0 = func_00354E98(temp_s4);
            var_a1_5 = 1;
            if (!(temp_f0 > 0x0.0p+0f)) {
                var_a1_5 = 0;
            }
            temp_a0_5 = M2C_FIELD(arg0, char **, 0x14);
            M2C_FIELD(temp_a0_5, s32 *, 0x1C) = (s32) ((M2C_FIELD(temp_a0_5, s32 *, 0x1C) & ~0xFF) | var_a1_5);
            if (var_a1_5 != 0) {
                temp_a1_16 = M2C_FIELD(arg0, char **, 0x14);
                temp_v0_28 = (VE3 *)((char *)M2C_FIELD(temp_a1_16, char **, 0x14) + 0x110);
                temp_v0_28->fn(temp_a1_16 + temp_v0_28->delta, temp_f0);
        float value_576 = func_00354EA8(temp_s4);
                temp_a1_17 = M2C_FIELD(arg0, char **, 0x14);
                temp_v0_29 = (VE3 *)((char *)M2C_FIELD(temp_a1_17, char **, 0x14) + 0x108);
                temp_v0_29->fn(temp_a1_17 + temp_v0_29->delta, value_576);
            }
        } else {
            temp_a0_6 = M2C_FIELD(arg0, char **, 0x14);
            M2C_FIELD(temp_a0_6, s32 *, 0x18) = (s32) (M2C_FIELD(temp_a0_6, s32 *, 0x18) & 0xFFFF00FF);
            temp_a2_9 = M2C_FIELD(arg0, char **, 0x14);
            temp_v0_30 = (VE4 *)((char *)M2C_FIELD(temp_a2_9, char **, 0x14) + 0xE8);
            temp_v0_30->fn(temp_a2_9 + temp_v0_30->delta, 0);
            temp_a0_7 = M2C_FIELD(arg0, char **, 0x14);
            M2C_FIELD(temp_a0_7, s32 *, 0x18) = (s32) (M2C_FIELD(temp_a0_7, s32 *, 0x18) & 0xFFFFFF);
            temp_v1_4 = M2C_FIELD(arg0, char **, 0x14);
            M2C_FIELD(temp_v1_4, s32 *, 0x1C) = (s32) (M2C_FIELD(temp_v1_4, s32 *, 0x1C) & ~0xFF);
        }
        float value_591 = (f32) func_003684E8(temp_s4) * 0x1.9999980000000p-4f;
        temp_a1_18 = M2C_FIELD(arg0, char **, 0x14);
        temp_v0_31 = (VE3 *)((char *)M2C_FIELD(temp_a1_18, char **, 0x14) + 0x118);
        temp_v0_31->fn(temp_a1_18 + temp_v0_31->delta, value_591);
        float value_594 = func_00368518(temp_s4);
        temp_a1_19 = M2C_FIELD(arg0, char **, 0x14);
        temp_v0_32 = (VE3 *)((char *)M2C_FIELD(temp_a1_19, char **, 0x14) + 0x120);
        temp_v0_32->fn(temp_a1_19 + temp_v0_32->delta, value_594);
    }
    temp_s1_4 = arg0 + 0x2D78;
    func_003DD7D8(arg0 + 0x17B8, M2C_FIELD(temp_s4, f32 *, 0x6C8));
    M2C_FIELD(arg0, f32 *, 0x1740) = (f32) M2C_FIELD(temp_s5, f32 *, 0x920);
    M2C_FIELD(arg0, f32 *, 0x17B0) = (f32) M2C_FIELD(arg0, f32 *, 0x17B8);
    temp_v0_33 = func_0035BEB0(temp_s4) == 1;
    M2C_FIELD(arg0, s32 *, 0x2D78) = (s32) ((M2C_FIELD(arg0, s32 *, 0x2D78) & ~0xFF) | temp_v0_33);
    if (temp_v0_33 != 0) {
        M2C_FIELD(temp_s1_4, f32 *, 0x20) = func_00354E60(temp_s4);
        M2C_FIELD(temp_s1_4, f32 *, 0x24) = func_00354E58(temp_s4);
    }
    temp_f0_2 = func_00344680(temp_s4, arg0 + 0x48);
    if (temp_f0_2 != 0x0.0p+0f) {
        M2C_FIELD(arg0, s32 *, 0x44) = (s32) (((s32) M2C_FIELD(arg0, s32 *, 0x44) & 0xFF00FFFF) | 0x10000);
    }
    temp_v0_34 = arg0 + 0x10E4;
    M2C_FIELD(temp_v0_34, f32 *, 0x1C) = (f32) (temp_f0_2 * -0x1.6800000000000p+6f);
    M2C_FIELD(temp_v0_34, f32 *, 0xC) = (f32) *fader;
    if (func_003683A8(temp_s4) != 0) {
        temp_s0_7 = arg0 + 0x3B4C;
        M2C_FIELD(temp_s0_7, s32 *, 0x24) = (s32) ((M2C_FIELD(temp_s0_7, s32 *, 0x24) & ~0xFF) | 1);
        func_00368420(temp_s4, &sp10);
        func_003AA850(temp_s0_7, sp10);
        func_003AA8D0(temp_s0_7, sp11);
        func_003AA9B8(temp_s0_7, sp12);
        func_003AAA38(temp_s0_7, sp13);
        M2C_FIELD(temp_s0_7, f32 *, 0x20) = (f32) ((f32) func_00368498(temp_s4) * 0x1.0000000000000p-3f);
    }
    func_003A4AA8(arg0 + 0x23DC, fparg0);
    func_003A4AA8(arg0 + 0x291C, fparg0);
    func_003BEE58(arg0 + 0x1108, fparg0);
    if (fparg0 > 0x0.0p+0f) {
        func_003A6638(arg0 + 0x2D14, fparg0);
        func_003A39B0(arg0 + 0x1170);
        func_003A4FC8(arg0 + 0x17BC, fparg0);
        func_003A4AA8(arg0 + 0x252C, fparg0);
        func_003A4AA8(arg0 + 0x267C, fparg0);
        func_003A4AA8(arg0 + 0x27CC, fparg0);
        func_003A4AA8(arg0 + 0x2A6C, fparg0);
        func_003A4AA8(arg0 + 0x2BBC, fparg0);
        func_003A4AA8(arg0 + 0x11B0, fparg0);
    }
    func_003ADFD8(sp20, fparg0);
    func_003AE330(arg0 + 0x3EEC, fparg0);
    if (isReplay(arg0)) {
        func_0039EE78(arg0);
    }
    M2C_FIELD(arg0, s8 *, 0x30) = func_0039F0C0(arg0, 1);
}

}
