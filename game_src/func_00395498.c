#include "types.h"
void *memcpy(void *, const void *, unsigned int);
#include "m2c_macros.h"

void *CourseData__getRunway(s32);                           /* extern */
f32 GT4Course__RunwayData__Section__getCheckPoint(void *, s32, s32);            /* extern */
f32 GT4Course__RunwayData__getCheckPoint(s32, s32, s32);               /* extern */

struct func_00395498_arg0 {
    char pad0[0x4];
    s32 unk4;
    char pad8[0xA8];
    s32 unkB0;
};

f32 RaceCourse__getCheckPoint(struct func_00395498_arg0 *arg0, s32 arg1, s32 arg2) {
    if (arg0->unkB0 < 0) {
        return GT4Course__RunwayData__Section__getCheckPoint(CourseData__getRunway(arg0->unk4), arg1, arg2);
    }
    return GT4Course__RunwayData__getCheckPoint(M2C_FIELD(CourseData__getRunway(arg0->unk4), s32 *, 0x9C) + (arg0->unkB0 << 5), arg1, arg2);
}
/* native correction of src/race/unit_00394D48/RaceCourse__getCheckPoint.c: returns the f32 ($f0) of the tail-called getCheckPoint functions (defined f32), which the callers read. */
