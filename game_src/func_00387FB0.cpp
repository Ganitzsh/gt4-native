/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native copy of the provisional draft for 00387FB0: the slot-0xA0 virtual gets arg1, which the EE forwards to it in $a1 (passthrough_lint) */
#include "types.h"
#define NULL 0
char *memcpy(char *, const char *, unsigned int);
#include "m2c_macros.h"

extern "C" {
s32 func_00346510(s32);                             /* extern */
s32 func_0034C190(char *, s32, char *);             /* extern */
void func_0034D418(char *, char *);              /* extern */
s32 func_00360458(char *);                          /* extern */
void func_00387CF0(char *, s32) throw();                 /* extern */
void func_00387E28(char *, M2C_UNK) throw();             /* extern */
void func_00389D30(char *, M2C_UNK);             /* extern */
void func_00391A60();                            /* extern */
void func_00391CA8();                            /* extern */
void func_00391D28();                            /* extern */
void func_00391D98(float);                     /* extern */
void func_00391E50(M2C_UNK);                     /* extern */
void func_00391E80(M2C_UNK);                     /* extern */
void func_003921A8(s32);                         /* extern */
void func_003922B0(s32);                         /* extern */
void func_00394D48(char *);                      /* extern */
void func_00394ED8(char *, s64);                 /* extern */
void func_00395078(char *, s64);                 /* extern */
void func_00397C38(s32, char *);                 /* extern */
void func_003B4D80(s32);                         /* extern */
void func_003B4E88(s32, M2C_UNK);                /* extern */
void func_003B5358(char *, s32, s32);            /* extern */
void func_003B53D8(s32);                         /* extern */
void func_003B5438(char *, s32, M2C_UNK, M2C_UNK); /* extern */
void func_003B5988(s32, s32);                    /* extern */
void func_003B5C08(s32, char *);                 /* extern */
void func_003B5CA0(s32);                         /* extern */
void func_003B5D00(s32);                         /* extern */
void func_003B5D90(s32);                         /* extern */
void func_003B6AE8(s32, s32);                    /* extern */
void func_003C0190(char *);                      /* extern */
void func_003DEC40(char *);                      /* extern */
void func_00426BC0(s32);                         /* extern */
s64 func_004454C0(s32);                             /* extern */
s32 func_00447550(s32);                             /* extern */
int func_00447CA0(s64, char *);              /* extern */
int func_00448820(s64, char *);              /* extern */
unsigned char func_00448968(char *);                       /* extern */
void func_0044CE00();                            /* extern */
void func_00462E18();                            /* extern */
s32 func_0057F238(char *, s32) throw();                  /* extern */

}
extern char *D_0061870C;
extern float D_006213DC, D_006213E0;
extern "C" void func_00387FB0(char *arg0, s32 arg1) {
    struct { int mode; char pad[12]; char spec[32]; char name[64]; } local __attribute__((aligned(16)));
    int type;
    M2C_UNK var_a1;
    M2C_UNK var_a1_2;
    M2C_UNK var_a3;
    s32 temp_a0;
    s32 temp_a1;
    s32 temp_a1_2;
    s32 temp_s0;
    s32 temp_s0_2;
    s32 temp_s0_3;
    s32 temp_s2;
    s32 temp_s5;
    s32 temp_s7;
    s32 temp_v0_11;
    s32 temp_v0_3;
    s32 temp_v0_4;
    s32 temp_v0_7;
    s32 temp_v0_8;
    s32 temp_v1_10;
    s32 temp_v1_12;
    s32 temp_v1_13;
    s32 temp_v1_15;
    s32 temp_v1_3;
    s32 temp_v1_5;
    s32 temp_v1_7;
    s32 temp_v1_8;
    s32 temp_v1_9;
    s32 var_s0;
    s32 var_s1;
    s32 var_v0;
    char *temp_a1_3;
    char *temp_a1_4;
    char *temp_a1_5;
    char *temp_a1_6;
    char *temp_a2;
    char *temp_a2_2;
    char *temp_s0_4;
    char *temp_s4;
    char *temp_s6;
    char *temp_v0;
    char *temp_v0_10;
    char *temp_v0_2;
    char *temp_v0_5;
    char *temp_v0_6;
    char *temp_v0_9;
    char *temp_v1;
    char *temp_v1_11;
    char *temp_v1_14;
    char *temp_v1_2;
    char *temp_v1_4;
    char *temp_v1_6;

    M2C_FIELD(arg0, s32 *, 0xD14) = 0;
    M2C_FIELD(arg0, s32 *, 0xD2C) = 0x7FFFFFFF;
    M2C_FIELD(arg0, s32 *, 0xCE0) = 0;
    M2C_FIELD(arg0, s32 *, 0xD18) = 0;
    M2C_FIELD(arg0, s32 *, 0xD28) = 0;
    float zero = 0.0f;
    M2C_FIELD(arg0, f32 *, 0xD30) = zero;
    if ((u32) M2C_FIELD(arg0, u32 *, 0xCC8) < 2U) {
        temp_v0 = M2C_FIELD(arg0, char **, 0x64);
        ({ struct E { short delta, index; void (*fn)(char *, s32); }; E *e = (E *)(temp_v0 + 0xA0); e->fn(arg0 + e->delta, arg1); });
    } else {
        M2C_FIELD(arg0, f32 *, 0xD34) = zero;
    }
    temp_s4 = M2C_FIELD(arg0, char **, 0x6C);
    M2C_FIELD(arg0, f32 *, 0xD3C) = 1.0f;
    M2C_FIELD(arg0, s32 *, 0xD38) = 0;
    M2C_FIELD(arg0, s32 *, 0xD40) = 0;
    M2C_FIELD(arg0, s32 *, 0xD44) = 0;
    M2C_FIELD(arg0, s32 *, 0xD4C) = 0;
    M2C_FIELD(arg0, s32 *, 0xD50) = 0;
    M2C_FIELD(arg0, s32 *, 0xD54) = 0;
    M2C_FIELD(arg0, s32 *, 0xD58) = 0;
    M2C_FIELD(arg0, s32 *, 0xD5C) = 0;
    func_003DEC40(temp_s4);
    *M2C_FIELD(arg0, char ***, 0x74) = temp_s4;
    func_00387CF0(arg0, arg1);
    var_a1 = 0;
    if ((M2C_FIELD(temp_s4, s32 *, 0xE0) != 0) || (M2C_FIELD(temp_s4, s32 *, 0xDC) != 0)) {
        var_a1 = 1;
    }
    var_s1 = 0;
    func_00387E28(arg0, var_a1);
    func_00389D30(arg0, 0);
    func_003C0190(temp_s4);
    M2C_FIELD(temp_s4, char **, 0x84) = arg0;
    temp_v1 = M2C_FIELD(arg0, char **, 0x64);
    func_00426BC0(({ struct E { short delta, index; s32 (*fn)(char *); }; E *e = (E *)(temp_v1 + 0x230); e->fn(arg0 + e->delta); }));
    func_0034D418(M2C_FIELD(arg0, char **, 0x70), temp_s4);
    temp_v0_2 = M2C_FIELD(arg0, char **, 0x64);
    ({ struct E { short delta, index; void (*fn)(char *); }; E *e = (E *)(temp_v0_2 + 0x2E0); e->fn(arg0 + e->delta); });
    temp_s5 = M2C_FIELD(M2C_FIELD(temp_s4, char **, 0x60), s32 *, 0);
    M2C_FIELD(M2C_FIELD(arg0, char **, 0x70), s32 *, 0xF8A0) = temp_s5;
    if (temp_s5 > 0) {
        do {
            temp_a2 = M2C_FIELD(temp_s4, char **, 0x60);
            temp_v0_3 = var_s1 * 4;
            temp_a1 = var_s1;
            var_s1 += 1;
            temp_s0 = *(s32 *)(temp_v0_3 + M2C_FIELD(temp_a2, s32 *, 8));
            func_003B6AE8(temp_s0, func_0034C190(M2C_FIELD(arg0, char **, 0x70), temp_a1, temp_a2));
            func_003B4D80(temp_s0);
            var_a1_2 = 0;
            if (((u32) M2C_FIELD(arg0, u32 *, 0xCC8) >= 2U) || (M2C_FIELD(temp_s4, s32 *, 0xF0) != 0) || (M2C_FIELD(temp_s4, s32 *, 0xDC) != 0)) {
                var_a1_2 = 1;
            }
            func_003B4E88(temp_s0, var_a1_2);
        } while (var_s1 < temp_s5);
    }
    type = 2;
    func_00394ED8(M2C_FIELD(temp_s4, char **, 0x80), M2C_FIELD(temp_s4, s64 *, 0x78));
    func_00395078(M2C_FIELD(temp_s4, char **, 0x80), M2C_FIELD(temp_s4, s64 *, 0x78));
    temp_v0_4 = M2C_FIELD(M2C_FIELD(M2C_FIELD(M2C_FIELD(M2C_FIELD(arg0, char **, 0x6C), char **, 0x80), char **, 4), char **, 0x80), s32 *, 0x10);
    temp_a1_2 = temp_v0_4 & 0x80000;
    if (temp_a1_2 == 0) {
        int bit = temp_v0_4 & 0x8000;
        type = bit != 0;
    }
    temp_v1_2 = M2C_FIELD(arg0, char **, 0x64);
    var_s1 = 0;
    local.mode = ({ struct E { short delta, index; s32 (*fn)(char *, s32); }; E *e = (E *)(temp_v1_2 + 0x1A0); e->fn(arg0 + e->delta, temp_a1_2); });
    if (temp_s5 > 0) {
        char *spec = local.spec;
        char *name = local.name;
        do {
        temp_v1_3 = var_s1 * 4;
        var_s1 += 1;
        char *car = *(char * *)(temp_v1_3 + M2C_FIELD(M2C_FIELD(temp_s4, char **, 0x60), s32 *, 8));
        s32 info = (s32)(car + 0x20);
        func_00448820(func_004454C0(info), spec);
        s32 cls = func_00448968(spec);
        s32 dirt = func_00447550(M2C_FIELD(temp_s4, s32 *, 0x70)) == 2;
        func_00447CA0(func_004454C0(info), name);
        var_s0 = func_0057F238(name, (s32)D_0061870C) == 0;
        temp_v1_4 = M2C_FIELD(arg0, char **, 0x64);
        int v = ({ struct E { short delta, index; s32 (*fn)(char *); }; E *e = (E *)(temp_v1_4 + 0xC8); e->fn(arg0 + e->delta); });
        int kind = var_s0 ? 1 : type;
        if (v == 0) {
            var_s0 = 0;
            if (cls >= 2 || (!dirt && cls != 0 && temp_s5 < 3)) var_s0 = 1;
        } else {
            var_s0 = cls >= 2;
        }
        temp_a2_2 = M2C_FIELD(car, char **, 0x2880);
        temp_v0_5 = M2C_FIELD(temp_a2_2, char **, 8);
        ({ struct E { short delta, index; void (*fn)(char *, s32); }; E *e = (E *)(temp_v0_5 + 0x158); e->fn(temp_a2_2 + e->delta, kind); });
        func_003B5358(car, local.mode, var_s0);
        } while (var_s1 < temp_s5);
    }
    var_s1 = 0;
    D_006213DC = 1.0f;
    func_00391E50(0);
    func_00391E80(0);
    func_00391CA8();
    if (temp_s5 > 0) {
        do {
            temp_v1_5 = var_s1 * 4;
            var_s1 += 1;
            func_003B5D90(*(s32 *)(temp_v1_5 + M2C_FIELD(M2C_FIELD(temp_s4, char **, 0x60), s32 *, 8)));
        } while (var_s1 < temp_s5);
    }
    func_0044CE00();
    func_00462E18();
    temp_a1_3 = M2C_FIELD(M2C_FIELD(M2C_FIELD(arg0, char **, 0x6C), char **, 0x80), char **, 4);
    func_00397C38(M2C_FIELD(temp_a1_3, s32 *, 0x80), temp_a1_3);
    temp_v0_6 = M2C_FIELD(arg0, char **, 0x64);
    ({ struct E { short delta, index; void (*fn)(char *); }; E *e = (E *)(temp_v0_6 + 0x138); e->fn(arg0 + e->delta); });
    temp_a1_4 = M2C_FIELD(arg0, char **, 0x70);
    temp_v1_6 = M2C_FIELD(temp_a1_4, char **, 0x10140);
    temp_v0_7 = ({ struct E { short delta, index; s32 (*fn)(char *); }; E *e = (E *)(temp_v1_6 + 0x50); e->fn(temp_a1_4 + e->delta); });
    switch (temp_v0_7) {
    case 0: M2C_FIELD(temp_s4, s32 *, 0xB4) = -1; break;
    case 1: break;
    case 2: M2C_FIELD(temp_s4, s32 *, 0xB4) = 3; break;
    case 3: M2C_FIELD(temp_s4, s32 *, 0xB4) = -1; break;
    }
    if (local.mode == 4) {
        for (var_s1 = 0; var_s1 < temp_s5; var_s1++)
            func_003B53D8(M2C_FIELD(M2C_FIELD(temp_s4, char **, 0x60), s32 **, 8)[var_s1]);
    }
    for (var_s1 = 0; var_s1 < temp_s5; var_s1++)
        func_003B5CA0(M2C_FIELD(M2C_FIELD(temp_s4, char **, 0x60), s32 **, 8)[var_s1]);
    func_0044CE00();
    for (var_s1 = 0; var_s1 < temp_s5; var_s1++)
        func_003B5C08(M2C_FIELD(M2C_FIELD(temp_s4, char **, 0x60), s32 **, 8)[var_s1], temp_s4);
    func_0044CE00();
    for (var_s1 = 0; var_s1 < temp_s5; var_s1++)
        func_003B5D00(M2C_FIELD(M2C_FIELD(temp_s4, char **, 0x60), s32 **, 8)[var_s1]);
    func_0044CE00();
    if ((local.mode ^ 4) != 0) {
        for (var_s1 = 0; var_s1 < temp_s5; var_s1++) {
            temp_v1_11 = M2C_FIELD(M2C_FIELD(temp_s4, char **, 0x60), char ***, 8)[var_s1];
            var_a3 = -1;
            switch (M2C_FIELD(temp_v1_11, s32 *, 0x345C)) {
            case 0:
                var_a3 = 0;
                break;
            case 1:
                var_a3 = 1;
                break;
            case 2:
                var_a3 = 2;
                break;
            }
            func_003B5438(temp_v1_11, temp_s5, 0, var_a3);
        }
    }
    for (var_s1 = 0; var_s1 < temp_s5; var_s1++) {
        temp_s0_4 = M2C_FIELD(M2C_FIELD(temp_s4, char **, 0x60), char ***, 8)[var_s1];
        temp_a1_5 = M2C_FIELD(temp_s0_4, char **, 0x2880);
        temp_v0_9 = M2C_FIELD(temp_a1_5, char **, 8);
        ({ struct E { short delta, index; void (*fn)(char *); }; E *e = (E *)(temp_v0_9 + 0x80); e->fn(temp_a1_5 + e->delta); });
        temp_a1_6 = M2C_FIELD(temp_s0_4, char **, 0x2880);
        temp_v0_10 = M2C_FIELD(temp_a1_6, char **, 8);
        ({ struct E { short delta, index; void (*fn)(char *, float); }; E *e = (E *)(temp_v0_10 + 0x48); e->fn(temp_a1_6 + e->delta, 200.0f); });
    }
    for (var_s1 = 0; var_s1 < temp_s5; var_s1++)
        func_003B5988(M2C_FIELD(M2C_FIELD(temp_s4, char **, 0x60), s32 **, 8)[var_s1], local.mode);
    func_0044CE00();
    if ((u32) (M2C_FIELD(arg0, u32 *, 0xCC8) - 2) < 2U) {
        temp_v1_14 = M2C_FIELD(arg0, char **, 0x64);
        temp_v0_11 = func_00346510(({ struct E { short delta, index; s32 (*fn)(char *, M2C_UNK); }; E *e = (E *)(temp_v1_14 + 0x240); e->fn(arg0 + e->delta, 0x100); }) + 0xDC);
        if (temp_v0_11 != 0) {
            M2C_FIELD(arg0, s32 *, 0xD2C) = temp_v0_11;
        }
    }
    func_00394D48(M2C_FIELD(M2C_FIELD(arg0, char **, 0x6C), char **, 0x80));
    func_00391A60();
    D_006213E0 = 1.0f;
    func_00391D28();
    if ((u32) (M2C_FIELD(arg0, u32 *, 0xCC8) - 2) < 2U) {
        var_s1 = 0;
        func_00391D98(3.0f);
    } else {
        var_s1 = 0;
        func_00391D98(0.3f);
    }
    if (temp_s5 > 0) {
        do {
            temp_v1_15 = var_s1 * 4;
            var_s1 += 1;
            func_003921A8(*(s32 *)(temp_v1_15 + M2C_FIELD(M2C_FIELD(temp_s4, char **, 0x60), s32 *, 8)) + 0x2900);
        } while (var_s1 < temp_s5);
    }
    func_003922B0(func_00360458(M2C_FIELD(arg0, char **, 0x70)));
}
