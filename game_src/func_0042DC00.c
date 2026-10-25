#include "types.h"
#define NULL 0
void *memcpy(void *, const void *, unsigned int);

f32 func_004860E8(s32, s32, f32);               /* extern */
f32 func_004861A8(s32, f32);                    /* extern */
s32 func_00486388(s32, s32, s32, f32 fparg0);                   /* extern */

struct func_0042DC00_arg0 {
    s32 unk0;
    s32 unk4;
};

f32 func_0042DC00(void *arg0, f32 fparg0) {
    s32 temp_s1;
    s32 temp_v0;

    temp_s1 = arg0 + 8;
    temp_v0 = func_00486388(temp_s1, ((struct func_0042DC00_arg0 *)arg0)->unk0, ((struct func_0042DC00_arg0 *)arg0)->unk4, fparg0);
    ((struct func_0042DC00_arg0 *)arg0)->unk4 = temp_v0;
    if (temp_v0 != 0) {
        return func_004861A8(temp_v0, fparg0);
    }
    ((struct func_0042DC00_arg0 *)arg0)->unk4 = temp_s1;
    return func_004860E8(temp_s1, ((struct func_0042DC00_arg0 *)arg0)->unk0, fparg0);
}
/* native correction of src/race/unit_004271E8/func_0042DC00.c (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_004860E8 */
/* native correction of src/race/unit_004271E8/func_0042DC00.c: returns the f32 ($f0) of the tail-called func_004861A8 / func_004860E8 (both defined f32), which func_0042DA58 and func_0042DAA0 read. */
