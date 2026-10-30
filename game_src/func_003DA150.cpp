/* Native copy of 003DA150 (matched), the race monitor: its 16:9-mode width 853.33 = 480 x 16/9 and x scale 0.75 become
 * 480 x aspect and (4/3) / aspect (gt4_wide_squeeze); ABS.S is fabsf. (docs/WIDESCREEN.md) */
extern "C" float gt4_wide_squeeze(void);
#include "types.h"
#include "m2c_macros.h"
extern char *D_00622F4C;
struct Global { char pad[0x20]; int mode; };
extern Global D_006184D0;
extern char D_00623838[];
extern "C" {
int func_003D8B78(char *) throw();
void func_003D9158(void *) throw();
void func_003D91C0(void *) throw();
void func_003D92B0(void *) throw();
void func_003DA0B8(void *, int) throw();
void func_003DA0E0(int, float) throw();
void func_001074D0(char *, int, int, int, int);
void func_00107AB8(int);
int RaceTooltipDisplay__render_main_2(void *);
void func_003EC0B0(int, int, int, int);
void func_0044E390(void *, float, float);
void func_004A1638(int);
void func_004A2980(int);
void func_004A3078(int);
void func_004A3230(int);
void RaceEntryCar__update(int);
void func_004A5A90(float, float, float, float, float, float);
void func_004A7454();
void func_004AB040(int);
}
static inline float width(Global *g) { if (g->mode) return 640.0f / gt4_wide_squeeze(); return 640.0f; }
static inline float scale(Global *g) { if (g->mode) return gt4_wide_squeeze(); return 1.0f; }
extern "C" void func_003DA150(char *arg0) {
    float var_f12;
    float var_f13;
    f32 temp_f12;
    f32 temp_f12_2;
    f32 temp_f1;
    s32 temp_a0;
    s32 temp_v1;
    s32 temp_v1_2;
    s32 temp_v1_3;
    s32 var_a2;
    u8 *var_s2;

    if (func_003D8B78(arg0) == 0) return func_00107AB8(1);
    {
        if (M2C_FIELD(arg0, s32 *, 0x24) != 0) {
            if (M2C_FIELD(arg0, s32 *, 0x34) == 1) {
                if (M2C_FIELD(D_00622F4C, s32 *, 0x3A360) == 0) {
                    func_004AB040(0x16);
                }
                func_004A2980(-1);
                func_004A3078(0);
                return func_004A3230(1);
            }
        } else {
            temp_f1 = M2C_FIELD(arg0, f32 *, 0x84);
            if (temp_f1 >= 0x1.0000000000000p+0f) {
                M2C_FIELD(arg0, s32 *, 0x1C) = 1;
            }
            temp_v1 = M2C_FIELD(arg0, s32 *, 0x28);
            if ((temp_v1 != 0) || ((temp_f1 > 0x0.0p+0f) && (M2C_FIELD(arg0, s32 *, 0x20) == 0))) {
                var_a2 = 0x80FFFFFF;
                if (temp_v1 != 1) {
                    var_a2 = ((s32) (temp_f1 * 0x1.0000000000000p+7f) << 0x18) | 0xFFFFFF;
                }
                temp_a0 = M2C_FIELD(arg0, s32 *, 8);
                if (temp_a0 != 0) {
                    func_003EC0B0(temp_a0, M2C_FIELD(arg0, s32 *, 4), var_a2, 0);
                }
            }
            var_s2 = (u8 *)(arg0 + 0x358);
            if (*var_s2 != 0) {
                RaceEntryCar__update(1);
                func_004A7454();
                var_f13 = width(&D_006184D0);
                func_004A5A90(0.0f, var_f13, 480.0f, 0.0f, -1.0f, 1.0f);
                RaceEntryCar__update(0);
                func_004A7454();
                var_f12 = scale(&D_006184D0);
                func_0044E390(D_00623838, var_f12, 1.0f);
                RaceTooltipDisplay__render_main_2(var_s2);
                func_0044E390(D_00623838, 1.0f, 1.0f);
            }
            temp_f12 = M2C_FIELD(arg0, f32 *, 0x80);
            if ((temp_f12 <= 0x0.0p+0f) && (M2C_FIELD(arg0, s32 *, 0x28) != M2C_FIELD(arg0, s32 *, 0x2C))) {
                temp_v1_2 = M2C_FIELD(arg0, s32 *, 0x2C);
                M2C_FIELD(arg0, s32 *, 0x28) = temp_v1_2;
                M2C_FIELD(arg0, s32 *, 0x30) = 1;
                switch (temp_v1_2) {                /* switch 1; irregular */
                case 0:                             /* switch 1 */
                    func_003D9158(arg0);
                    break;
                case 1:                             /* switch 1 */
                    func_003D91C0(arg0);
                    break;
                case 2:                             /* switch 1 */
                    func_003D92B0(arg0);
                    break;
                }
            } else {
                M2C_FIELD(arg0, s32 *, 0x30) = 0;
            }
            if ((M2C_FIELD(arg0, f32 *, 0x80) == -0x1.0000000000000p+0f) && (M2C_FIELD(arg0, s32 *, 0x28) == 1)) {
                func_003DA0B8(arg0, 4);
            }
            temp_f12_2 = __builtin_fabsf(M2C_FIELD(arg0, f32 *, 0x80));
            if (temp_f12_2 < 0x1.0000000000000p+0f) {
                func_003DA0E0(0, temp_f12_2);
            }
            if (M2C_FIELD(arg0, s32 *, 0x34) != M2C_FIELD(arg0, s32 *, 0x38)) {
                temp_v1_3 = M2C_FIELD(arg0, s32 *, 0x38);
                M2C_FIELD(arg0, s32 *, 0x34) = temp_v1_3;
                switch (temp_v1_3) {                /* switch 2; irregular */
                case 0:                             /* switch 2 */
                    if (M2C_FIELD(D_00622F4C, s32 *, 0x3A360) == 0) {
                        func_001074D0((char *)&D_006184D0, 1, 1, 0x280, 0x1C0);
                        func_004AB040(0x16);
                    }
                    func_003DA0B8(arg0, 1);
                    break;
                case 1:                             /* switch 2 */
                    if (M2C_FIELD(D_00622F4C, s32 *, 0x3A360) == 0) {
                        func_001074D0((char *)&D_006184D0, 1, 2, 0x280, 0x1C0);
                        func_004A1638(0x16);
                    }
                    break;
                }
            }
        }
    }
}
