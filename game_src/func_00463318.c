#include "types.h"
#define NULL 0
void *memcpy(void *, const void *, unsigned int);

void func_00462D30(s32, f32);                    /* extern */
void func_00576090(void *);                      /* extern */
void func_005760A8(void *, s32);             /* extern */
void func_00576100(void *);                      /* extern */
void func_00576140(void *);                      /* extern */

void func_00463318(s32 arg0, f32 fparg0) {
    s8 sp[0x10];
    func_00576090(sp);
    func_00576100(sp);
    func_00462D30(arg0, fparg0);
    func_00576140(sp);
    func_005760A8(sp, 2);
}
/* native correction of src/engine/unit_00462900/func_00463318.c (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_00462D30 */
